#include <SPI.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <TP229.h>

/* ================== PIN DEFINITIONS ================== */
// TFT Display (ST7735S)
#define TFT_CS   6
#define TFT_DC   7
#define TFT_RST  8

// SPI (Arduino Micro hardware SPI)
#define TFT_MOSI 16
#define TFT_SCK  15

// Keypad (TP229)
#define KP_SCL   2
#define KP_SDA   3

// Sensors
#define LS1 5
#define LS2 4
#define LS3 11
#define LS4 10

// Outputs
#define SIRENE    9
#define ALARM_LED 12

/* ================== OBJECTS ================== */
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
TP229 keypad(KP_SCL, KP_SDA);

/* ================== EEPROM ================== */
#define EEPROM_MAGIC  0xA7
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_ADDR_CODE  1
#define EEPROM_ADDR_ALARM 5

/* ================== STATES ================== */
bool alarmArmed = false;
bool alarmLatched = false;

/* ================== CODE ENTRY ================== */
char codeBuf[5] = "";
uint8_t codeLen = 0;

/* ================== BLINK ================== */
unsigned long lastBlink = 0;
bool blinkState = false;

/* ================== FUNCTIONS ================== */
bool codeStored() {
  return EEPROM.read(EEPROM_ADDR_MAGIC) == EEPROM_MAGIC;
}

void readCode(char *dst) {
  for (int i = 0; i < 4; i++) dst[i] = EEPROM.read(EEPROM_ADDR_CODE + i);
  dst[4] = '\0';
}

bool checkCode() {
  char stored[5];
  readCode(stored);
  return strcmp(codeBuf, stored) == 0;
}

void clearCodeBuffer() {
  memset(codeBuf, 0, sizeof(codeBuf));
  codeLen = 0;
}

void latchAlarmIfTriggered() {
  if (!alarmArmed) return;
  if (!digitalRead(LS1) || !digitalRead(LS2) || !digitalRead(LS3) || !digitalRead(LS4)) {
    alarmLatched = true;
  }
}

void showAlarmBlink() {
  if (millis() - lastBlink < 250) return;
  lastBlink = millis();
  blinkState = !blinkState;

  tft.fillScreen(blinkState ? ST77XX_RED : ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.print("ALARM!");
  tft.setTextSize(1);
  tft.setCursor(10, 70);
  tft.print("Code eingeben");
}

void acknowledgeAlarm() {
  alarmLatched = false;
  alarmArmed = false;
  EEPROM.update(EEPROM_ADDR_ALARM, 0);
  noTone(SIRENE);
  digitalWrite(ALARM_LED, LOW);
  tft.fillScreen(ST77XX_BLACK);
}

/* ================== SETUP ================== */
void setup() {
  pinMode(SIRENE, OUTPUT);
  pinMode(ALARM_LED, OUTPUT);

  pinMode(LS1, INPUT_PULLUP);
  pinMode(LS2, INPUT_PULLUP);
  pinMode(LS3, INPUT_PULLUP);
  pinMode(LS4, INPUT_PULLUP);

  SPI.begin();
  tft.initR(INITR_144GREENTAB);
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);

  keypad.begin();

  if (codeStored()) {
    alarmArmed = EEPROM.read(EEPROM_ADDR_ALARM);
  } else {
    tft.setCursor(10, 40);
    tft.setTextColor(ST77XX_YELLOW);
    tft.print("Bitte Code setzen");
  }
}

/* ================== LOOP ================== */
void loop() {
  latchAlarmIfTriggered();

  if (alarmLatched) {
    digitalWrite(ALARM_LED, HIGH);
    tone(SIRENE, 1200);
    showAlarmBlink();
  }

  keypad.update();
  char key = keypad.getKey();

  if (key >= '0' && key <= '9' && codeLen < 4) {
    codeBuf[codeLen++] = key;
    codeBuf[codeLen] = '\0';
  }

  if (key == '#') {
    if (checkCode()) {
      acknowledgeAlarm();
    }
    clearCodeBuffer();
  }

  if (key == '*') {
    clearCodeBuffer();
  }
}