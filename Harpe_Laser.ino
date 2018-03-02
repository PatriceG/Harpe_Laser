#include <Adafruit_MCP4725.h>
#include <Wire.h>
#include <MIDI.h>
//#define DEBUG

#define PIN_LED_BUILTIN 11
#define PIN_LED_LASER_ON 12
#define PIN_LASER_GREEN 10
#define PIN_LASER_RED 9
#define PIN_SENSOR 7 //A7

#define DAC_ADDR 0x64

#define MIDI_CHANNEL 1
#define MIDI_VELOCITY 127

#define NUM_STRINGS 9
uint8_t stringToNoteArray[] = { 60, 62, 64, 65, 67, 69, 71, 72, 74 };

#define LASER_ON_TIME 1500
//#define LASER_ON_TIME 2000000u
#define LASER_SETTLE_TIME 1500
//#define LASER_SETTLE_TIME 1000000u
//harp max width in % of galvo deflection
#define HARP_MAX_WIDTH 100
#define HARP_OPENING_SPEED 0.5
#define NB_DAC_VALUES 4096

#define SENSOR_DELTA_THRESHOLD 2.0
#define SENSOR_CALIBRATION_VALUES 75

uint16_t strings[NUM_STRINGS]; //string positions

uint8_t stringStatus[NUM_STRINGS]; //string (note) status for note on/off detection

enum status { CLOSED, OPENING, CALIBRATING, OPEN, CLOSING };
status harpStatus = CLOSED;
float harpWidth = 0;
int harp1stStringOffset = 0;

int sensorThreshold = 0;
uint16_t loopCounter = 0;
int8_t stringIndex = 0;
int8_t stringInc = 1; 

Adafruit_MCP4725 dac;
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

void setup()
{
#ifdef DEBUG
	Serial.begin(115200);
	delay(4000);
#endif
	pinMode(PIN_LED_BUILTIN, OUTPUT);
	pinMode(PIN_LED_LASER_ON, OUTPUT);
	pinMode(PIN_LASER_GREEN, OUTPUT);
	pinMode(PIN_LASER_RED, OUTPUT);	
	pinMode(PIN_SENSOR, INPUT);
	
	dac.begin(DAC_ADDR);

	//for now the harp starts open and in calibrating status
	delay(2000);
	calcStrings(HARP_MAX_WIDTH);
	//harpStatus = CALIBRATING;
	harpStatus = OPENING;
	digitalWrite(PIN_LED_LASER_ON, HIGH);
}

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

	//when opening the harp, recalculate the string position each time the first string is lit
	if (harpStatus == OPENING && string == 0) {
		calcStrings(harpWidth);
		harpWidth += HARP_OPENING_SPEED;
		if (harpWidth >= HARP_MAX_WIDTH) {
			harpStatus = CALIBRATING;
		}
	}
}

uint8_t stringToNote(uint8_t string) {
	uint8_t note = 0;
	if (string < NUM_STRINGS) {
		note = stringToNoteArray[string];
	}
	return note;
}

void noteOn(uint8_t string) {
#ifndef DEBUG
	usbMIDI.sendNoteOn(stringToNote(string), MIDI_VELOCITY, MIDI_CHANNEL);
#else
	Serial.println("noteOn");
#endif
}
void noteOff(uint8_t string) {
#ifndef DEBUG
	usbMIDI.sendNoteOff(stringToNote(string), 0, MIDI_CHANNEL);
#else
	Serial.println("noteOff");
#endif
}

void moveLaser(uint16_t position) {
	//actual move
	dac.setVoltage(position, false);
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

void laserBeam(uint8_t pin, uint8_t state) {
	//for now only handle green laser
	digitalWrite(pin, state);
}

void loop()
{
	uint16_t position = strings[stringIndex];
	laserBeam(PIN_LASER_GREEN, LOW);
	moveLaser(position + harp1stStringOffset);
	laserBeam(PIN_LASER_GREEN, HIGH);
	delayMicroseconds(LASER_ON_TIME);

	manageSensor(stringIndex);

	//if we're on last string, reverse direction
	if (stringIndex == NUM_STRINGS-1) {
		stringIndex = -1;
		//stringInc = -1;
	}
	//if we're on first string, reverse direction 
	//if (stringIndex == 0) {
		//stringInc = 1;
	//}


	stringIndex += stringInc;
  
	loopCounter++;

	
}
