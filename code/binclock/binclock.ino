// NOTICE: This code has been tested with an ATTINY85 and an Arduino Uno.
// To make it work with one or the other, make the sure right lines
// are commented/uncommented as noted below.

// Uncomment to use Arduino instead
//#include <Wire.h>

// Comment out to use Arduino instead
#include <TinyWireM.h>

#include <Adafruit_MCP23017.h>
#define Wire TinyWireM

// Change this to whatever you want
const int SNOOZE_MINUTES = 3;

// Arduino pins - Uncomment to use Arduino instead
//const int SCL_PIN = A5;
//const int SDA_PIN = A4;
//const int CLK_PIN = 2;
//const int SER_PIN = 3;
//const int LAT_PIN = 4;

// ATTINY85 pins - Comment out to use Arduino instead
const int SCL_PIN = 2;
const int SDA_PIN = 0;
const int CLK_PIN = 1;
const int SER_PIN = 3;
const int LAT_PIN = 4;

// Input button "pins" (bits) on port expander
const int BTN1_EXP_PIN = 12;
const int BTN2_EXP_PIN = 13;
const int BTN3_EXP_PIN = 14;
const int BTN4_EXP_PIN = 15;

// DS3231 realtime clock address
const int RTC_ADDR = 0x68;

// MCP23017 port expander access
Adafruit_MCP23017 mcp;

byte buttonState;
byte prevButtonState;
byte hour, minute, second;
byte prevSecond;
byte alarmHour, alarmMinute;
bool alarmEnabled;
bool alarmOn;
bool snoozeOn;
byte snoozeHour, snoozeMinute, snoozeSecond;

int alarmDuration;
int alarmFrequency;

byte displayMode; // 0 all, 1 hide sec, 2 hide min, 3 hide hr, 4 hide alarm

void setup() {
  Wire.begin();
  mcp.begin(); // Use default MCP23017 I2C address
  for (int i = 0; i < 11; i++) {
    mcp.pinMode(i, OUTPUT);
  }
  for (int i = BTN1_EXP_PIN; i <= BTN4_EXP_PIN; i++) {
    mcp.pinMode(i, INPUT);
    mcp.pullUp(i, HIGH);
  }

  pinMode(CLK_PIN, OUTPUT);
  pinMode(SER_PIN, OUTPUT);
  pinMode(LAT_PIN, OUTPUT);
}

// Convert normal decimal numbers to binary coded decimal
byte decToBcd(byte val)
{
  return( (val/10*16) + (val%10) );
}
// Convert binary coded decimal to normal decimal numbers
byte bcdToDec(byte val)
{
  return( (val/16*10) + (val%16) );
}

void writeTime() {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0); // set next input to start at the seconds register
  Wire.write(decToBcd(second));
  Wire.write(decToBcd(minute));
  Wire.write(decToBcd(hour));
  Wire.write(decToBcd(1)); // day of week (1=Sunday, 7=Saturday)
  Wire.write(decToBcd(1)); // day of month (1 to 31)
  Wire.write(decToBcd(1)); // month
  Wire.write(decToBcd(0)); // year (0 to 99)
  Wire.endTransmission();
}

void readTime() {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0); // set DS3231 register pointer to 00h
  Wire.endTransmission();
  Wire.requestFrom(RTC_ADDR, 3);
  second = bcdToDec(Wire.read() & 0x7f);
  minute = bcdToDec(Wire.read());
  hour = bcdToDec(Wire.read() & 0x3f);
}

void writeAlarm() {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x07); // second, minute, hour
  byte alarmSecond = alarmEnabled ? 1 : 0;
  Wire.write(decToBcd(alarmSecond));
  Wire.write(decToBcd(alarmMinute));
  Wire.write(decToBcd(alarmHour));
  Wire.endTransmission();
}

void readAlarm() {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x07); // second, minute, hour
  Wire.endTransmission();
  Wire.requestFrom(RTC_ADDR, 3);
  alarmEnabled = bcdToDec(Wire.read() & 0b01111111) > 0;
  alarmMinute = bcdToDec(Wire.read() & 0b01111111);
  alarmHour = bcdToDec(Wire.read() & 0b01111111);
}

void updateDisplay(byte seconds, byte minutes, byte hours, int indicator) {
  //  [seconds]      [minutes]    [hours/indicator]
  // G G G G G G    G G G G G G    G G G G G R     H/M/S/Indicator LEDs, LSB first
  //                        0 1    2 3 4 5 6 7     74HC595 Shift Register, Qa-h
  // 0 1 2 3 4 5    6 7 0 1                        MCP23017 Port Expander, GPA0-GPB1

  for (int i = 0; i < 6; i++) {
    mcp.digitalWrite(i, bitRead(seconds, i));
  }
  for (int i = 6; i < 10; i++) {
    mcp.digitalWrite(i, bitRead(minutes, i - 6));
  }
  byte srBits = 0;
  bitWrite(srBits, 0, bitRead(minutes, 4));
  bitWrite(srBits, 1, bitRead(minutes, 5));
  bitWrite(srBits, 2, bitRead(hours, 0));
  bitWrite(srBits, 3, bitRead(hours, 1));
  bitWrite(srBits, 4, bitRead(hours, 2));
  bitWrite(srBits, 5, bitRead(hours, 3));
  bitWrite(srBits, 6, bitRead(hours, 4));
  bitWrite(srBits, 7, indicator);
  digitalWrite(LAT_PIN, LOW);
  digitalWrite(CLK_PIN, LOW);
  shiftOut(SER_PIN, CLK_PIN, MSBFIRST, srBits);
  digitalWrite(LAT_PIN, HIGH);
}

void setBeeping(boolean enable) {
  mcp.digitalWrite(10, enable);
}

void readButtonState() {
  buttonState = 0;
  bitWrite(buttonState, 0, mcp.digitalRead(BTN1_EXP_PIN));
  bitWrite(buttonState, 1, mcp.digitalRead(BTN2_EXP_PIN));
  bitWrite(buttonState, 2, mcp.digitalRead(BTN3_EXP_PIN));
  bitWrite(buttonState, 3, mcp.digitalRead(BTN4_EXP_PIN));
}

bool buttonIsDown(byte buttonNum) {
  return bitRead(buttonState, buttonNum) == 0;
}

bool buttonWasDown(byte buttonNum) {
  return bitRead(prevButtonState, buttonNum) == 0;
}

bool buttonPushed(byte buttonNum) {
  return buttonIsDown(buttonNum) && !buttonWasDown(buttonNum);
}

void turnAlarmOn() {
  alarmOn = true;
  snoozeOn = false;
}

void loop() {
  readAlarm();
  while (true) {
    readButtonState();
    readTime();
    boolean alarmChanged = false;
    boolean timeChanged = false;
    if (buttonIsDown(3)) {
      readAlarm();
      if (buttonPushed(0)) {
        alarmChanged = true;
        alarmMinute++;
        if (alarmMinute == 60) {
          alarmMinute = 0;
        }
      } else if (buttonPushed(1)) {
        alarmChanged = true;
        alarmHour++;
        if (alarmHour == 24) {
          alarmHour = 0;
        }
      } else if (buttonPushed(2)) {
        alarmOn = false;
        snoozeOn = false;
        alarmChanged = true;
        alarmEnabled = !alarmEnabled;
      }
    } else if (buttonIsDown(2)) {
      if (buttonPushed(0)) {
        timeChanged = true;
        minute++;
        if (minute == 60) {
          minute = 0;
        }
      } else if (buttonPushed(1)) {
        timeChanged = true;
        hour++;
        if (hour == 24) {
          hour = 0;
        }
      }
    } else if (buttonIsDown(0) && buttonPushed(1)) {
      displayMode++;
      if (displayMode == 5) {
        displayMode = 0;
      }
    } else if (alarmOn && buttonPushed(0)) {
      if (!snoozeOn) {
        snoozeOn = true;
        alarmOn = false;
        snoozeSecond = second;
        snoozeHour = hour;
        snoozeMinute = minute + SNOOZE_MINUTES;
        if (snoozeMinute > 59) {
          snoozeMinute = 60 - snoozeMinute;
          snoozeHour += 1;
          if (snoozeHour == 24) {
            snoozeHour = 0;
          }
        }
      }
    } else if (buttonIsDown(1) && buttonPushed(0)) {
      turnAlarmOn();
    }

    if (alarmEnabled && second == 0 && minute == alarmMinute && hour == alarmHour) {
      turnAlarmOn();
    }

    if (snoozeOn && second == snoozeSecond && minute == snoozeMinute && hour == snoozeHour) {
      turnAlarmOn();
    }

    if (alarmChanged) {
      writeAlarm();
    }

    if (timeChanged) {
      second = 0;
      writeTime();
    }

if (buttonIsDown(3)) {
      updateDisplay(0, alarmMinute, alarmHour, alarmEnabled);
    } else {
      boolean indicator = (displayMode != 4 && !alarmOn && !snoozeOn && alarmEnabled) || ((alarmOn || snoozeOn) && (second % 2 == 0));
      if (displayMode == 1) {
        updateDisplay(0, minute, hour, indicator);
      } else if (displayMode == 2) {
        updateDisplay(0, 0, hour, indicator);
      } else if (displayMode == 3) {
        updateDisplay(0, 0, 0, indicator);
      } else if (displayMode == 4) {
        updateDisplay(0, 0, 0, indicator);
      } else { // 0
        updateDisplay(second, minute, hour, indicator);
      }
    }

    bool shouldBeep = alarmOn && second % 2 == 0;
    if (shouldBeep) {
      setBeeping(true);
      updateDisplay(0, 0, 0, true);
    } else {
      setBeeping(false);
    }

    prevButtonState = buttonState;
    prevSecond = second;
    for (int i = 0; i < 10 && buttonState == prevButtonState && second == prevSecond; i++) {
      delay(100);
      readTime();
      if (second == prevSecond) {
        readButtonState();
      }
    }
  }
}
