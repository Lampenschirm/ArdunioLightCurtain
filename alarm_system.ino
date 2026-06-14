#include <SPI.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <TP229.h>

// ================== DISPLAY (ST7735S 1.44" 128x128) ==================
#define TFT_CS   6
#define TFT_DC   7     // A0-Pin am Display
#define TFT_RST  8
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ================== IO ==================
#define SIRENE            9   // <-- nicht 6, weil TFT_CS=6!
#define LICHTSCHRANKE1    5
#define LICHTSCHRANKE2    4
#define LICHTSCHRANKE3   11
#define LICHTSCHRANKE4   10
#define ALARM_LED        12

// ================== KEYPAD (TP229, 2-wire) ==================
TP229 keypad(2, 3);

// ================== TP229 Key-Mapping ==================
static const char keyMap[17] = {
/*0*/  0,
/*1*/ '1', /*2*/ '2', /*3*/ '3', /*4*/ 'A',
/*5*/ '4', /*6*/ '5', /*7*/ '6', /*8*/ 'B',
/*9*/ '7', /*10*/'8', /*11*/'9', /*12*/'C',
/*13*/'*', /*14*/'0', /*15*/'#', /*16*/'D'
};

// ================== EEPROM Layout ==================
const int EEPROM_MAGIC_ADDR   = 0; // 1 Byte
const int EEPROM_CODE_ADDR    = 1; // 4 Bytes: Code[0..3]
const int EEPROM_ALARM_ADDR   = 5; // 1 Byte: Alarm scharf? (0/1)
const uint8_t EEPROM_MAGIC    = 0xA7;

// ================== Einheitliche Sensorlogik ==================
// true  -> Sensor ist aktiv bei LOW  (typisch mit INPUT_PULLUP)
// false -> Sensor ist aktiv bei HIGH
const bool SENSOR_ACTIVE_LOW = false;

// ================== Sirene ==================
// Für PKM22EPP-40 gut hörbar im Bereich ca. 3.2 kHz - 4.3 kHz
int sirenenFreq = 3500;
bool sireneAufwaerts = true;

// ================== Alarm Latch + Blink ==================
bool alarmLatched = false;          // bleibt TRUE bis quittiert
bool blinkState = false;
unsigned long lastBlinkMs = 0;
const unsigned long BLINK_INTERVAL_MS = 250;  // Flackertakt

// Welche Lichtschranke hat ausgelöst? Bit0=LS1, Bit1=LS2, Bit2=LS3, Bit3=LS4
uint8_t alarmSourceMask = 0;

// ================== Einschaltverzögerung ==================
bool armingPending = false;                    // wartet auf Scharfschaltung
unsigned long armingStartMs = 0;
const unsigned long ARMING_DELAY_MS = 30000;  // 30 Sekunden
int lastShownArmingSec = -1;                  // für Countdown-Refresh

// ================== Piepton während Countdown ==================
bool countdownBeepActive = false;
unsigned long countdownBeepEndMs = 0;
int lastBeepSecond = -1;

const int COUNTDOWN_BEEP_FREQ = 4000;         // gut für PKM22EPP-40
const unsigned long COUNTDOWN_BEEP_MS = 80;   // kurzer Piepton

// ================== UI / State ==================
enum UiState {
  UI_SET1,
  UI_SET2,
  UI_CODE,
  UI_STATUS,
  UI_DIAG,
  UI_RESET_CONFIRM,
  UI_ALARM
};
UiState uiState = UI_CODE;

enum CodeAction {
  ACT_TOGGLE_ALARM,
  ACT_CHANGE_CODE,
  ACT_FACTORY_RESET,
  ACT_ACK_ALARM
};
CodeAction codeAction = ACT_TOGGLE_ALARM;

bool alarmArmed  = false;  // Alarm scharf?

char codeBuf[5]  = {0};    // Eingabe
uint8_t codeLen  = 0;

char newCode1[5] = {0};    // Setzen 1
char newCode2[5] = {0};    // Setzen 2

uint8_t lastKeyRaw = 0;

// Flags für Set-Code-Flows
bool initialSetupFlow = false;


// ------------------ Helpers ------------------
bool isDigit(char c) {
  return (c >= '0' && c <= '9');
}

bool eepromHasCode() {
  return EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC;
}

void eepromReadCode(char outCode[5]) {
  for (int i = 0; i < 4; i++) {
    outCode[i] = (char)EEPROM.read(EEPROM_CODE_ADDR + i);
  }
  outCode[4] = '\0';
}

void eepromWriteCode(const char inCode[5]) {
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
  for (int i = 0; i < 4; i++) {
    EEPROM.update(EEPROM_CODE_ADDR + i, (uint8_t)inCode[i]);
  }
}

void eepromWriteAlarm(bool armed) {
  EEPROM.update(EEPROM_ALARM_ADDR, armed ? 1 : 0);
}

bool eepromReadAlarm() {
  return EEPROM.read(EEPROM_ALARM_ADDR) == 1;
}

void resetCodeEntry() {
  codeLen = 0;
  codeBuf[0] = '\0';
}

void takeCodeInto(char target[5]) {
  for (int i = 0; i < 4; i++) {
    target[i] = codeBuf[i];
  }
  target[4] = '\0';
}

bool codesMatch(const char a[5], const char b[5]) {
  for (int i = 0; i < 4; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

bool checkAgainstStoredCode() {
  char stored[5];
  eepromReadCode(stored);
  for (int i = 0; i < 4; i++) {
    if (codeBuf[i] != stored[i]) return false;
  }
  return true;
}

// Einheitliche Sensorauswertung
bool sensorTriggered(uint8_t pin) {
  int v = digitalRead(pin);
  return SENSOR_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

// Countdown: verbleibende Sekunden (30 ... 1 ... 0)
int getArmingRemainingSec() {
  if (!armingPending) return 0;

  unsigned long now = millis();
  unsigned long elapsed = now - armingStartMs;
  if (elapsed >= ARMING_DELAY_MS) return 0;

  unsigned long remainingMs = ARMING_DELAY_MS - elapsed;
  return (int)((remainingMs + 999UL) / 1000UL); // aufrunden
}

void startCountdownBeep() {
  countdownBeepActive = true;
  countdownBeepEndMs = millis() + COUNTDOWN_BEEP_MS;
  tone(SIRENE, COUNTDOWN_BEEP_FREQ);
}

void updateCountdownBeep() {
  if (!countdownBeepActive) return;

  if (millis() >= countdownBeepEndMs) {
    countdownBeepActive = false;

    // Nur abschalten, wenn kein echter Alarm aktiv ist
    if (!alarmLatched) {
      noTone(SIRENE);
    }
  }
}

void captureAlarmSources(bool ls1, bool ls2, bool ls3, bool ls4) {
  alarmSourceMask = 0;

  if (ls1) alarmSourceMask |= 0x01;
  if (ls2) alarmSourceMask |= 0x02;
  if (ls3) alarmSourceMask |= 0x04;
  if (ls4) alarmSourceMask |= 0x08;
}


// ------------------ Display helpers ------------------
void drawHomeHint() {
  // oben rechts, damit showMsg unten frei bleibt
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(78, 0);
  tft.print("B=Home");
}

void drawHeader(const char* title) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 8);
  tft.print(title);
}

void drawSensorStatusLine(int y, const char* label, bool active) {
  tft.setTextSize(1);

  tft.setCursor(8, y);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(label);
  tft.print(": ");

  if (active) {
    tft.setTextColor(ST77XX_RED);
    tft.print("BLOCK");
  } else {
    tft.setTextColor(ST77XX_GREEN);
    tft.print("OK");
  }
}

void drawSkull(int x, int y) {
  // Puls-Animation
  int r = blinkState ? 16 : 13;
  int yOffset = blinkState ? 0 : 2;

  // Knochen-Bewegung
  int boneOffset = blinkState ? 4 : 1;

  // ===== Kopf =====
  tft.drawCircle(x, y + yOffset, r, ST77XX_WHITE);

  // Unterkiefer
  tft.drawRoundRect(x - r + 3, y + 6 + yOffset, (r - 3) * 2, 10, 5, ST77XX_WHITE);

  // ===== Augen =====
  tft.drawCircle(x - 6, y - 3 + yOffset, 4, ST77XX_WHITE);
  tft.drawCircle(x + 6, y - 3 + yOffset, 4, ST77XX_WHITE);

  // ===== Nase =====
  tft.fillTriangle(
    x, y + 1 + yOffset,
    x - 3, y + 6 + yOffset,
    x + 3, y + 6 + yOffset,
    ST77XX_WHITE
  );

  // ===== Mund =====
  tft.drawLine(x - 7, y + 10 + yOffset, x + 7, y + 10 + yOffset, ST77XX_WHITE);

  // ===== Zähne =====
  tft.drawLine(x - 3, y + 10 + yOffset, x - 3, y + 14 + yOffset, ST77XX_WHITE);
  tft.drawLine(x + 3, y + 10 + yOffset, x + 3, y + 14 + yOffset, ST77XX_WHITE);

  // ===== Animierte Knochen =====
  // LINKS
  tft.drawCircle(x - r - boneOffset, y - 4 + yOffset, 3, ST77XX_WHITE);
  tft.drawCircle(x - r - boneOffset, y + 4 + yOffset, 3, ST77XX_WHITE);

  // RECHTS
  tft.drawCircle(x + r + boneOffset, y - 4 + yOffset, 3, ST77XX_WHITE);
  tft.drawCircle(x + r + boneOffset, y + 4 + yOffset, 3, ST77XX_WHITE);
}

void drawStarsLine() {
  uint16_t bg = ST77XX_BLACK;

  // Im Alarm-Blinkmodus Hintergrund anpassen
  if (uiState == UI_ALARM && blinkState) {
    bg = ST77XX_RED;
  }

  int y = (uiState == UI_ALARM) ? 114 : 55;

  tft.fillRect(8, y, 112, 14, bg);
  tft.setCursor(8, y);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  for (uint8_t i = 0; i < codeLen; i++) tft.print('*');
  for (uint8_t i = codeLen; i < 4; i++) tft.print('_');
}

void drawCodeLine(const char* label) {
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 40);
  tft.print(label);

  drawStarsLine();

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(8, 90);
  tft.print("#=OK   *=Clear");
}

void showMsg(uint16_t color, const char* msg) {
  tft.fillRect(0, 110, 128, 18, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.setCursor(8, 114);
  tft.print(msg);

  // Home-Hinweis auf allen normalen Screens nachzeichnen
  if (uiState != UI_ALARM) {
    drawHomeHint();
  }
}

void drawSetCode1Screen() {
  drawHeader("Neuen Code setzen");
  drawCodeLine("Code (4 Ziffern):");
  drawHomeHint();
}

void drawSetCode2Screen() {
  drawHeader("Code bestaetigen");
  drawCodeLine("Nochmal eingeben:");
  drawHomeHint();
}

void drawEnterCodeScreen() {
  drawHeader("Code eingeben");

  tft.setTextSize(1);
  tft.setCursor(8, 22);
  tft.setTextColor(ST77XX_YELLOW);

  if (codeAction == ACT_TOGGLE_ALARM) {
    tft.print("Aktion: Alarm ");
    tft.print((alarmArmed || armingPending) ? "AUS" : "EIN");
  } else if (codeAction == ACT_CHANGE_CODE) {
    tft.print("Aktion: Code aendern");
  } else if (codeAction == ACT_FACTORY_RESET) {
    tft.print("Aktion: RESET");
  } else {
    tft.print("Aktion: QUITTIERE");
  }

  drawCodeLine("Code (4 Ziffern):");
  drawHomeHint();
}

void drawResetConfirmScreen() {
  drawHeader("RESET bestaetigen");
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_RED);
  tft.setCursor(8, 28);
  tft.print("Urzustand!");
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 46);
  tft.print("Code & Status loeschen");
  tft.setCursor(8, 62);
  tft.print("Alarm -> AUS");
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(8, 90);
  tft.print("#=JA   *=Abbruch");
  drawHomeHint();
}

void drawStatusScreen() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);
  tft.setCursor(8, 22);

  if (armingPending) {
    int sec = getArmingRemainingSec();

    tft.setTextColor(ST77XX_YELLOW);
    tft.print("SCHARF");
    tft.setCursor(8, 44);
    tft.print("IN ");
    if (sec < 10) tft.print(" ");
    tft.print(sec);
    tft.print("s");
  } else if (alarmArmed) {
    tft.setTextColor(ST77XX_RED);
    tft.print("ALARM");
    tft.setCursor(8, 44);
    tft.print("EIN");
  } else {
    tft.setTextColor(ST77XX_GREEN);
    tft.print("ALARM");
    tft.setCursor(8, 44);
    tft.print("AUS");
  }

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 76);
  tft.print("# = Alarm ");
  tft.print((alarmArmed || armingPending) ? "AUS" : "EIN");

  tft.setCursor(8, 88);
  tft.print("A = Diagnose");

  tft.setCursor(8, 100);
  tft.print("D = Code  C = Reset");

  drawHomeHint();
}

void drawDiagScreen() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(8, 8);
  tft.print("DIAGNOSE LICHTSCHRANKEN");

  bool s1 = sensorTriggered(LICHTSCHRANKE1);
  bool s2 = sensorTriggered(LICHTSCHRANKE2);
  bool s3 = sensorTriggered(LICHTSCHRANKE3);
  bool s4 = sensorTriggered(LICHTSCHRANKE4);

  drawSensorStatusLine(30, "LS1", s1);
  drawSensorStatusLine(44, "LS2", s2);
  drawSensorStatusLine(58, "LS3", s3);
  drawSensorStatusLine(72, "LS4", s4);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(8, 100);
  tft.print("A/* = Zurueck");

  drawHomeHint();
}

void drawAlarmCodeScreenBase() {
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(28, 8);
  tft.print("ALARM");

  // Pulsierender Totenkopf mit animierten Knochen
  drawSkull(64, 36);

  // Ausloeser anzeigen
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(8, 62);
  tft.print("Ausloeser:");

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 74);

  if (alarmSourceMask == 0) {
    tft.print("Unbekannt");
  } else {
    bool first = true;

    if (alarmSourceMask & 0x01) {
      tft.print("LS1");
      first = false;
    }
    if (alarmSourceMask & 0x02) {
      if (!first) tft.print(" ");
      tft.print("LS2");
      first = false;
    }
    if (alarmSourceMask & 0x04) {
      if (!first) tft.print(" ");
      tft.print("LS3");
      first = false;
    }
    if (alarmSourceMask & 0x08) {
      if (!first) tft.print(" ");
      tft.print("LS4");
    }
  }

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(8, 96);
  tft.print("Code zum Stoppen:");

  drawStarsLine();

  // Im Alarmmodus absichtlich kein B=Home-Hinweis.
}


// ------------------ Factory Reset ------------------
void factoryResetNow() {
  alarmLatched = false;
  alarmArmed = false;
  armingPending = false;
  lastShownArmingSec = -1;
  countdownBeepActive = false;
  lastBeepSecond = -1;
  alarmSourceMask = 0;

  digitalWrite(ALARM_LED, LOW);
  noTone(SIRENE);

  EEPROM.update(EEPROM_MAGIC_ADDR, 0);
  for (int i = 0; i < 4; i++) EEPROM.update(EEPROM_CODE_ADDR + i, 0);
  EEPROM.update(EEPROM_ALARM_ADDR, 0);

  initialSetupFlow = true;
  uiState = UI_SET1;
  resetCodeEntry();
  drawSetCode1Screen();
  showMsg(ST77XX_GREEN, "RESET OK");
}

// ------------------ Alarm acknowledge ------------------
void acknowledgeAlarmAndDisarm() {
  alarmLatched = false;
  alarmArmed = false;
  armingPending = false;
  lastShownArmingSec = -1;
  countdownBeepActive = false;
  lastBeepSecond = -1;
  alarmSourceMask = 0;

  eepromWriteAlarm(false);

  noTone(SIRENE);
  digitalWrite(ALARM_LED, LOW);

  uiState = UI_STATUS;
  drawStatusScreen();
  showMsg(ST77XX_GREEN, "Alarm aus");
}

// ------------------ Alarm / Sensor ------------------
void starteSirene() {
  tone(SIRENE, sirenenFreq);

  // schnellerer Sweep -> wirkt aggressiver / lauter
  sirenenFreq += sireneAufwaerts ? 60 : -60;

  if (sirenenFreq >= 4300) sireneAufwaerts = false;
  if (sirenenFreq <= 3200) sireneAufwaerts = true;
}

void updateAlarmLatch() {
  bool ls1 = sensorTriggered(LICHTSCHRANKE1);
  bool ls2 = sensorTriggered(LICHTSCHRANKE2);
  bool ls3 = sensorTriggered(LICHTSCHRANKE3);
  bool ls4 = sensorTriggered(LICHTSCHRANKE4);

  bool triggered = (ls1 || ls2 || ls3 || ls4);

  // Latch: wenn scharf und getriggert -> bleibt an bis quittiert
  if (alarmArmed && triggered) {
    if (!alarmLatched) {
      alarmLatched = true;

      // Merken, welche Lichtschranke ausgelöst hat
      captureAlarmSources(ls1, ls2, ls3, ls4);

      // Sirene definiert starten
      sirenenFreq = 3500;
      sireneAufwaerts = true;

      // Countdown sicher beenden
      armingPending = false;
      countdownBeepActive = false;
      lastShownArmingSec = -1;
      lastBeepSecond = -1;

      // UI in Alarmmodus wechseln
      uiState = UI_ALARM;
      codeAction = ACT_ACK_ALARM;
      resetCodeEntry();
      lastBlinkMs = 0;
      blinkState = false;
    }
  }
}

void updateAlarmOutputs() {
  if (alarmLatched) {
    digitalWrite(ALARM_LED, HIGH);
    starteSirene();
  } else {
    digitalWrite(ALARM_LED, LOW);

    // Während des Countdown-Piepens NICHT sofort wieder abschalten
    if (!countdownBeepActive) {
      noTone(SIRENE);
    }
  }
}

void updateAlarmBlinkScreen() {
  if (uiState != UI_ALARM) return;

  unsigned long now = millis();
  if (now - lastBlinkMs >= BLINK_INTERVAL_MS) {
    lastBlinkMs = now;
    blinkState = !blinkState;

    // Flackern: rot/schwarz
    tft.fillScreen(blinkState ? ST77XX_RED : ST77XX_BLACK);
    drawAlarmCodeScreenBase();
  }
}

// ------------------ Einschaltverzögerung + Countdown ------------------
void updateArmingDelay() {
  if (!armingPending) return;

  unsigned long now = millis();
  unsigned long elapsed = now - armingStartMs;

  int remainingSec = getArmingRemainingSec();

  // Countdown nur bei Änderung der Sekunde neu zeichnen
  if (remainingSec != lastShownArmingSec) {
    lastShownArmingSec = remainingSec;

    if (uiState == UI_STATUS) {
      drawStatusScreen();
    }

    // Piepton einmal pro Sekunde, solange der Countdown läuft
    if (remainingSec > 0 && remainingSec != lastBeepSecond) {
      lastBeepSecond = remainingSec;
      startCountdownBeep();
    }
  }

  if (elapsed >= ARMING_DELAY_MS) {
    armingPending = false;
    alarmArmed = true;
    lastShownArmingSec = -1;
    lastBeepSecond = -1;
    countdownBeepActive = false;

    eepromWriteAlarm(true);
    noTone(SIRENE);

    if (uiState == UI_STATUS) {
      drawStatusScreen();
      showMsg(ST77XX_GREEN, "Alarm scharf");
    }
  }
}


// ------------------ Setup / Loop ------------------
void setup() {
  Serial.begin(9600);

  pinMode(SIRENE, OUTPUT);
  pinMode(ALARM_LED, OUTPUT);

  pinMode(LICHTSCHRANKE1, INPUT_PULLUP);
  pinMode(LICHTSCHRANKE2, INPUT_PULLUP);
  pinMode(LICHTSCHRANKE3, INPUT_PULLUP);
  pinMode(LICHTSCHRANKE4, INPUT_PULLUP);

  keypad.begin();

  SPI.begin();
  tft.initR(INITR_144GREENTAB);   // 1.44" ST7735 init
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);

  bool hasCode = eepromHasCode();
  alarmArmed = hasCode ? eepromReadAlarm() : false;
  armingPending = false;
  lastShownArmingSec = -1;
  countdownBeepActive = false;
  lastBeepSecond = -1;
  alarmSourceMask = 0;

  if (!hasCode) {
    initialSetupFlow = true;
    uiState = UI_SET1;
    resetCodeEntry();
    drawSetCode1Screen();
  } else {
    uiState = UI_STATUS;
    drawStatusScreen();
  }
}

void loop() {
  // 1) Alarm-Latch aktualisieren + Ausgänge setzen
  updateAlarmLatch();
  updateAlarmOutputs();

  // 2) Einschaltverzögerung + Countdown aktualisieren
  updateArmingDelay();

  // 3) Piepton-Zeitverwaltung
  updateCountdownBeep();

  // 4) Alarm-Screen blinken (non-blocking)
  updateAlarmBlinkScreen();

  // 5) Diagnose-Screen live aktualisieren
  static unsigned long lastDiagUpdate = 0;
  if (uiState == UI_DIAG && millis() - lastDiagUpdate >= 200) {
    lastDiagUpdate = millis();
    drawDiagScreen();
  }

  // 6) Keypad lesen (edge detection)
  keypad.update();
  uint8_t keyRaw = keypad.getPressedKey();

  if (keyRaw == 0) {
    lastKeyRaw = 0;
  } else if (keyRaw != lastKeyRaw) {
    lastKeyRaw = keyRaw;
    char k = (keyRaw <= 16) ? keyMap[keyRaw] : 0;
    if (k) handleKey(k);
  }

  delay(10);
}


// ------------------ Key Handling ------------------
void handleKey(char k) {
  // ===== GLOBAL HOME =====
  if (k == 'B') {
    // Während aktivem Alarm aus Sicherheitsgründen NICHT verlassen
    if (uiState == UI_ALARM) {
      return;
    }

    // Erstinbetriebnahme / nach Reset: Code-Setzen nicht umgehen
    if ((uiState == UI_SET1 || uiState == UI_SET2) && initialSetupFlow) {
      showMsg(ST77XX_YELLOW, "Code zuerst setzen");
      return;
    }

    resetCodeEntry();
    uiState = UI_STATUS;
    drawStatusScreen();
    return;
  }

  // Clear / Abbruch
  if (k == '*') {
    resetCodeEntry();

    if (uiState == UI_SET1) drawSetCode1Screen();
    else if (uiState == UI_SET2) drawSetCode2Screen();
    else if (uiState == UI_CODE) drawEnterCodeScreen();
    else if (uiState == UI_STATUS) drawStatusScreen();
    else if (uiState == UI_DIAG) {
      uiState = UI_STATUS;
      drawStatusScreen();
    }
    else if (uiState == UI_RESET_CONFIRM) drawResetConfirmScreen();
    else if (uiState == UI_ALARM) {
      // Alarm-Screen neu zeichnen
      tft.fillScreen(blinkState ? ST77XX_RED : ST77XX_BLACK);
      drawAlarmCodeScreenBase();
    }
    return;
  }

  // RESET Confirm Screen
  if (uiState == UI_RESET_CONFIRM) {
    if (k == '#') {
      factoryResetNow();
    }
    return;
  }

  // STATUS: Shortcuts
  if (uiState == UI_STATUS) {
    if (k == '#') {                 // Alarm togglen -> Code erforderlich
      codeAction = ACT_TOGGLE_ALARM;
      uiState = UI_CODE;
      resetCodeEntry();
      drawEnterCodeScreen();
      return;
    }
    if (k == 'D') {                 // Code ändern -> aktueller Code erforderlich
      codeAction = ACT_CHANGE_CODE;
      uiState = UI_CODE;
      resetCodeEntry();
      drawEnterCodeScreen();
      return;
    }
    if (k == 'C') {                 // Factory Reset -> aktueller Code erforderlich
      codeAction = ACT_FACTORY_RESET;
      uiState = UI_CODE;
      resetCodeEntry();
      drawEnterCodeScreen();
      return;
    }
    if (k == 'A') {                 // Diagnose-Modus
      uiState = UI_DIAG;
      drawDiagScreen();
      return;
    }
    return;
  }

  // Diagnose-Modus
  if (uiState == UI_DIAG) {
    if (k == 'A' || k == '*') {
      uiState = UI_STATUS;
      drawStatusScreen();
    }
    return;
  }

  // Ziffern sammeln (auch im Alarm-UI_ALARM)
  if (isDigit(k)) {
    if (codeLen < 4) {
      codeBuf[codeLen++] = k;
      codeBuf[codeLen] = '\0';
      drawStarsLine();
    }
    return;
  }

  // OK / Enter
  if (k == '#') {
    if (codeLen != 4) {
      showMsg(ST77XX_YELLOW, "4 Ziffern!");
      return;
    }

    // --- SETUP 1: neuen Code aufnehmen ---
    if (uiState == UI_SET1) {
      takeCodeInto(newCode1);
      resetCodeEntry();
      uiState = UI_SET2;
      drawSetCode2Screen();
      return;
    }

    // --- SETUP 2: bestaetigen ---
    if (uiState == UI_SET2) {
      takeCodeInto(newCode2);

      if (!codesMatch(newCode1, newCode2)) {
        showMsg(ST77XX_RED, "Codes ungleich!");
        resetCodeEntry();
        uiState = UI_SET1;
        drawSetCode1Screen();
        return;
      }

      // speichern
      eepromWriteCode(newCode1);

      // erstes Setzen -> Alarm sicher AUS
      if (initialSetupFlow) {
        alarmArmed = false;
        armingPending = false;
        lastShownArmingSec = -1;
        countdownBeepActive = false;
        lastBeepSecond = -1;
        alarmSourceMask = 0;
        eepromWriteAlarm(false);
        initialSetupFlow = false;
      } else {
        // bei Code-Änderung: Alarmzustand beibehalten
        eepromWriteAlarm(alarmArmed);
      }

      uiState = UI_STATUS;
      drawStatusScreen();
      showMsg(ST77XX_GREEN, "Code gespeichert");
      return;
    }

    // --- CODE SCREEN oder ALARM SCREEN: aktuellen Code prüfen ---
    if (uiState == UI_CODE || uiState == UI_ALARM) {
      if (!checkAgainstStoredCode()) {
        showMsg(ST77XX_RED, "Falscher Code!");
        resetCodeEntry();
        drawStarsLine();
        return;
      }

      // korrekt:
      if (uiState == UI_ALARM || codeAction == ACT_ACK_ALARM) {
        // Sirene stoppen + Alarm unscharf
        acknowledgeAlarmAndDisarm();
        return;
      }

      if (codeAction == ACT_TOGGLE_ALARM) {
        // Wenn Alarm bereits scharf ODER Scharfschaltung läuft -> ausschalten
        if (alarmArmed || armingPending) {
          alarmArmed = false;
          armingPending = false;
          alarmLatched = false;
          lastShownArmingSec = -1;
          countdownBeepActive = false;
          lastBeepSecond = -1;
          alarmSourceMask = 0;

          eepromWriteAlarm(false);

          noTone(SIRENE);
          digitalWrite(ALARM_LED, LOW);

          uiState = UI_STATUS;
          drawStatusScreen();
          showMsg(ST77XX_GREEN, "Alarm AUS");
          return;
        }

        // Wenn Alarm aus -> 30 Sekunden Einschaltverzögerung starten
        armingPending = true;
        armingStartMs = millis();
        lastShownArmingSec = -1; // Countdown-Refresh erzwingen
        countdownBeepActive = false;
        lastBeepSecond = -1;
        alarmSourceMask = 0;
        alarmArmed = false;

        eepromWriteAlarm(false);

        uiState = UI_STATUS;
        drawStatusScreen();
        showMsg(ST77XX_YELLOW, "Scharf in 30s");
        return;
      }

      if (codeAction == ACT_CHANGE_CODE) {
        // neuen Code setzen (2x)
        resetCodeEntry();
        uiState = UI_SET1;
        drawSetCode1Screen();
        showMsg(ST77XX_YELLOW, "Neuen Code eingeben");
        return;
      }

      if (codeAction == ACT_FACTORY_RESET) {
        uiState = UI_RESET_CONFIRM;
        drawResetConfirmScreen();
        return;
      }
    }
  }
}
