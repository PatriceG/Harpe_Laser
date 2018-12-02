#include <SPI.h>
#include <MIDI.h>
//when DEBUG is defined the harp sends debug messages instead of midi messages
//NOTE: the Teensy device type must be set accordingly in the project settings
//#define DEBUG

//ping mappings
#define PIN_LED_BUILTIN 11
#define PIN_LED_LASER_ON 12
#define PIN_LASER_GREEN 10
#define PIN_LASER_RED 9
#define PIN_SENSOR 7 //A7
#define PIN_DAC_LATCH 4
#define PIN_DAC_CS 5


//midi settings
#define MIDI_CHANNEL 1
#define MIDI_VELOCITY 127

//define this if you want red strings between green ones
//comment-out to have only green strings
#define ENABLE_RED_STRINGS

//number of strings of the harp and string to MIDI note mappings
#define NUM_STRINGS 10
uint8_t stringToNoteArray[] = { 60, 62, 64, 65, 67, 69, 71, 72, 74, 76 };
//transposition offset in semitones from the notes defines in stringToNoteArray
int8_t transposeOffset = 0;

//times in micro-seconds
#define LASER_ON_TIME 1500
#define LASER_SETTLE_TIME 1500

//harp max width in % of galvo deflection
#define HARP_MAX_WIDTH 100
#define HARP_OPENING_SPEED 0.5

//10-bit DAC
#define NB_DAC_VALUES 4096

//sensor sensitivity settings
#define SENSOR_DELTA_THRESHOLD 2.1
#define SENSOR_CALIBRATION_VALUES 75

//string positions
uint16_t strings[NUM_STRINGS]; 
//string 'angular offset' for vibration simulation
float stringVibes[NUM_STRINGS]; 
//string (note) status for note on/off detection
uint8_t stringStatus[NUM_STRINGS]; 

//harp states definitions
enum status { CLOSED, OPENING, CALIBRATING, OPEN, CLOSING };
status harpStatus = CLOSED;

//various variables
float harpWidth = 0;
int harp1stStringOffset = 0;
int sensorThreshold = 0;
uint16_t loopCounter = 0;
int8_t stringIndex = 0;
int8_t stringInc = 1; 



void initStringVibes() {
	for (uint8_t i = 0; i < NUM_STRINGS; i++) {
		stringVibes[i] = 0;		
	}
}
/* 
* calculate strings positions given a width percentage
*/
void calcStrings(float width) {
		uint16_t dacStep = (int)(((float)NB_DAC_VALUES / (float)NUM_STRINGS) * (width/100));
		uint16_t dacValue = 0;
		for (uint8_t i = 0; i < NUM_STRINGS; i++) {
			strings[i] = dacValue;
			dacValue += dacStep;
		}
		harp1stStringOffset = (NB_DAC_VALUES  - strings[NUM_STRINGS - 1]) / 2;
		if (harp1stStringOffset < 0)
			harp1stStringOffset = 0;
}
/*
* Handle incoming MIDI messages from the pedal-board
*/
void myNoteOn(byte channel, byte note, byte velocity) {	
	switch (note) {
		case 48: //first footwitch toggles the harp open/closed status
			if (harpStatus == CLOSED) {				
				harpStatus = OPENING;
			}
			else {
				harpStatus = CLOSING;
			}
			break;
		case 49: //second footswitch transposes down one octave
			transposeOffset -= 12;
			break;
		case 50: //third footswitch transposes up one octave
			transposeOffset += 12;
			break;
		case 51: //4th footswitch transposes down one semitone
			transposeOffset -= 1;
			break;
		case 52: //third footswitch transposes up one semitone
			transposeOffset += 1;
			break;
	}
}
void setup()
{
#ifdef DEBUG
	Serial.begin(115200);
	delay(4000);
#else
	usbMIDI.setHandleNoteOn(myNoteOn);
#endif
	pinMode(PIN_LED_BUILTIN, OUTPUT);
	pinMode(PIN_LED_LASER_ON, OUTPUT);
	pinMode(PIN_LASER_GREEN, OUTPUT);
	pinMode(PIN_LASER_RED, OUTPUT);	
	pinMode(PIN_SENSOR, INPUT);
	pinMode(PIN_DAC_LATCH, OUTPUT);
	pinMode(PIN_DAC_CS, OUTPUT);
	
	digitalWrite(PIN_DAC_CS, HIGH);
	
	//Init SPI DAC
	SPI.begin();

	//for now the harp starts in the opening state
	delay(2000);
	initStringVibes();
	calcStrings(HARP_MAX_WIDTH);	
	harpStatus = OPENING;
	digitalWrite(PIN_LED_LASER_ON, HIGH);
}

/*
* manage "string vibration" effect for each "plucked" string
*/
void manageStringVibes() {
	for (uint8_t i = 0; i < NUM_STRINGS; i++) {
		if (stringStatus[i]) {
			stringVibes[i] += 6.283189 / 32;
		}
		else {
			stringVibes[i] = 0;
		}
	}
}

uint16_t getStringVibeOffset(uint8_t stringIndex) {
	return (int)(sin(stringVibes[stringIndex]) * 20);
}
/*
* Manage sensor and harp states
*/
void manageSensor(uint8_t string) {
	static int calibrationValues[SENSOR_CALIBRATION_VALUES];
	static int calibrationIndex = 0;
	int sensorValue = analogRead(PIN_SENSOR);
	if (sensorThreshold != 0 && harpStatus == OPEN) {
		//playing the harp
#ifdef DEBUG
		Serial.print("sensorValue = ");
		Serial.print(sensorValue);
		Serial.print(" , sensorThreshold = ");
		Serial.println(sensorThreshold);
#endif
		if (sensorValue > sensorThreshold) {
			digitalWrite(PIN_LED_BUILTIN, HIGH);
			//string being played
			if (stringStatus[string] == 0) {
				stringStatus[string] = 1;
				noteOn(string);
			}
		}
		else {
			digitalWrite(PIN_LED_BUILTIN, LOW);
			//string not being played
			if (stringStatus[string] == 1) {
				stringStatus[string] = 0;
				noteOff(string);
			}
		}
		return;
	}
	if (harpStatus == CALIBRATING) {
		//we'll only pass once in the CALIBRATING state by power-on clycle
		if (calibrationIndex < SENSOR_CALIBRATION_VALUES) {
			digitalWrite(PIN_LED_BUILTIN, HIGH);
			calibrationValues[calibrationIndex++] = sensorValue;			
		}
		else {
			//calculate average + threshold
			unsigned long max = 0;
			for (uint8_t i = 0; i < SENSOR_CALIBRATION_VALUES; i++) {
				max += calibrationValues[i];
			}
			float avg = (float)max / (float)SENSOR_CALIBRATION_VALUES;
			sensorThreshold = (int)(avg * SENSOR_DELTA_THRESHOLD);
			digitalWrite(PIN_LED_BUILTIN, LOW);
#ifdef DEBUG
			Serial.print("sensorThreshold = ");			
			Serial.print(sensorThreshold);
			Serial.print(" , max = ");
			Serial.println(max);
#endif
			harpStatus = OPEN;
		}
	}

	//when opening or closing the harp, recalculate the string position each time the first string is lit
	if ((harpStatus == OPENING || harpStatus == CLOSING) && string == 0) {
		calcStrings(harpWidth);
		if (harpStatus == OPENING) {
			harpWidth += HARP_OPENING_SPEED;
			if (harpWidth >= HARP_MAX_WIDTH) {
				harpStatus = CALIBRATING;
			}
		}
		else{
			harpWidth -= HARP_OPENING_SPEED;
			if (harpWidth <= 0) {
				harpWidth = 0;				
				harpStatus = CLOSED;
			}
		}
		
	}
}

/*
* Return the MIDI note corresponding to the specified String, taking into account the current transposition level 
*/
uint8_t stringToNote(uint8_t string) {
	uint8_t note = 0;
	if (string < NUM_STRINGS) {
		note = stringToNoteArray[string];
		//apply current transposition value
		note += transposeOffset;
	}
	return note;
}

/*
* send MIDI note-on or debug message
*/
void noteOn(uint8_t string) {
#ifndef DEBUG
	usbMIDI.sendNoteOn(stringToNote(string), MIDI_VELOCITY, MIDI_CHANNEL);
#else
	Serial.println("noteOn");
#endif
}
/*
* send MIDI note-off or debug message
*/
void noteOff(uint8_t string) {
#ifndef DEBUG
	usbMIDI.sendNoteOff(stringToNote(string), 0, MIDI_CHANNEL);
#else
	Serial.println("noteOff");
#endif
}

void setDacVoltage(uint8_t dacId, uint16_t value) {
	byte low = value & 0xff;
	byte high = (value >> 8) & 0x0f;
	dacId = (dacId & 1) << 7;	
	digitalWrite(PIN_DAC_LATCH, HIGH);
	digitalWrite(PIN_DAC_CS, LOW);
	SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
	SPI.transfer(dacId | 0x30 | high);
	SPI.transfer(low);
	SPI.endTransaction();
	digitalWrite(PIN_DAC_CS, HIGH);
	//immediately latch dac value
	digitalWrite(PIN_DAC_LATCH, LOW);
}

/*
* Move laser beam to calculated string position
*/
void moveLaser(uint16_t position) {
	//actual move
	setDacVoltage(0, position);
#ifdef DEBUG
	Serial.print("MoveLaser(");
	Serial.print(position);
	Serial.println(")");
#endif
	//allow time to galvo to settle into position
	delayMicroseconds(LASER_SETTLE_TIME);	
#ifdef DEBUG
	Serial.println("done.");
#endif
}

/*
* drive the laser beam(s)
*/
void laserBeam(uint8_t pin, uint8_t state) {
	//for now only handle green laser
	digitalWrite(pin, state);
}

/*
* Main harp loop.
* Illuminates each string, simulates vibrations for "plucked" strings and calls manageSensor()
*/
void loop()
{
#ifndef DEBUG
	usbMIDI.read();
#endif
	uint16_t position = strings[stringIndex];
	laserBeam(PIN_LASER_GREEN, LOW);
#ifdef ENABLE_RED_STRINGS
	laserBeam(PIN_LASER_RED, LOW);
#endif
	if (harpStatus != CLOSED) {
		manageStringVibes();
		uint16_t vibeOffset = getStringVibeOffset(stringIndex);
		moveLaser(position + harp1stStringOffset + vibeOffset);

#ifdef ENABLE_RED_STRINGS
		//each third string is red, others are green
		if (stringIndex % 3 == 0) {
			laserBeam(PIN_LASER_RED, HIGH);
		}
		else {
			laserBeam(PIN_LASER_GREEN, HIGH);
		}
#else
		laserBeam(PIN_LASER_GREEN, HIGH);
#endif
		delayMicroseconds(LASER_ON_TIME);
		manageSensor(stringIndex);

		//if we're on last string, go back to first one
		if (stringIndex == NUM_STRINGS - 1) {
			stringIndex = -1;			
		}
	}
	stringIndex += stringInc;
	loopCounter++;
}
