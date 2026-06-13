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

// ================== Sirene ==================
// Für PKM22EPP-40 besser im Bereich ca. 3 kHz - 4.5 kHz
int sirenenFreq = 3500;
bool sireneAufwaerts = true;

// ================== Alarm Latch + Blink ==================
bool alarmLatched = false;          // bleibt TRUE bis quittiert
bool blinkState = false;
unsigned long lastBlinkMs = 0;
const unsigned long BLINK_INTERVAL_MS = 250;  // Flackertakt

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
enum UiState { UI_SET1, UI_SET2, UI_CODE, UI_STATUS, UI_RESET_CONFIRM, UI_ALARM };
UiState uiState = UI_CODE;

enum CodeAction { ACT_TOGGLE_ALARM, ACT_CHANGE_CODE, ACT_FACTORY_RESET, ACT_ACK_ALARM };
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
bool isDigit(char c) { return (c >= '0' && c <= '9'); }

bool eepromHasCode() {
  return EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC;
}

void eepromReadCode(char outCode[5]) {
  for (int i = 0; i < 4; i++) outCode[i] = (char)EEPROM.read(EEPROM_CODE_ADDR + i);
  outCode[4] = '\0';
}

void eepromWriteCode(const char inCode[5]) {
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
  for (int i = 0; i < 4; i++) EEPROM.update(EEPROM_CODE_ADDR + i, (uint8_t)inCode[i]);
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
  for (int i = 0; i < 4; i++) target[i] = codeBuf[i];
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

// ------------------ Display helpers ------------------
void drawHeader(const char* title) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 8);
  tft.print(title);
}

void drawStarsLine() {
  uint16_t bg = ST77XX_BLACK;

  // Im Alarm-Blinkmodus Hintergrund anpassen
  if (uiState == UI_ALARM && blinkState) {
    bg = ST77XX_RED;
  }

  tft.fillRect(8, 55, 112, 14, bg);
  tft.setCursor(8, 55);
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
}

void drawSetCode1Screen() {
  drawHeader("Neuen Code setzen");
  drawCodeLine("Code (4 Ziffern):");
}

void drawSetCode2Screen() {
  drawHeader("Code bestaetigen");
  drawCodeLine("Nochmal eingeben:");
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
}

void drawStatusScreen() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);
  tft.setCursor(8, 25);

  if (armingPending) {
    int sec = getArmingRemainingSec();

    tft.setTextColor(ST77XX_YELLOW);
    tft.print("SCHARF");
    tft.setCursor(8, 47);
    tft.print("IN ");
    if (sec < 10) tft.print(" ");
    tft.print(sec);
    tft.print("s");
  } else if (alarmArmed) {
    tft.setTextColor(ST77XX_RED);
    tft.print("ALARM");
    tft.setCursor(8, 47);
    tft.print("EIN");
  } else {
    tft.setTextColor(ST77XX_GREEN);
    tft.print("ALARM");
    tft.setCursor(8, 47);
    tft.print("AUS");
  }

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 78);
  tft.print("# = Alarm ");
  tft.print((alarmArmed || armingPending) ? "AUS" : "EIN");

  tft.setCursor(8, 90);
  tft.print("D = Code aendern");

  tft.setCursor(8, 102);
  tft.print("C = RESET (Urzustand)");
}

void drawAlarmCodeScreenBase() {
  // Grundlayout für Alarm-Quittierung (Blinkfarbe wird separat gesetzt)
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(18, 12);
  tft.print("ALARM");

  tft.setTextSize(1);
  tft.setCursor(8, 40);
  tft.print("Code zum Stoppen:");

  drawStarsLine();

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(8, 90);
  tft.print("#=OK   *=Clear");
}

// ------------------ Factory Reset ------------------
void factoryResetNow() {
  alarmLatched = false;
  alarmArmed = false;
  armingPending = false;
  lastShownArmingSec = -1;
  countdownBeepActive = false;
  lastBeepSecond = -1;

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

  // Sweep für hörbaren Sirenen-Effekt mit PKM22EPP-40
  sirenenFreq += sireneAufwaerts ? 20 : -20;

  if (sirenenFreq >= 4500) sireneAufwaerts = false;
  if (sirenenFreq <= 3000) sireneAufwaerts = true;
}

void updateAlarmLatch() {
  bool ls1 = digitalRead(LICHTSCHRANKE1);
  bool ls2 = digitalRead(LICHTSCHRANKE2);
  bool ls3 = digitalRead(LICHTSCHRANKE3);
  bool ls4 = digitalRead(LICHTSCHRANKE4);

  bool triggered = (ls1 || ls2 || ls3 || ls4);

  // Latch: wenn scharf und getriggert -> bleibt an bis quittiert
  if (alarmArmed && triggered) {
    if (!alarmLatched) {
      alarmLatched = true;

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

  // 5) Keypad lesen (edge detection)
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
  // Clear / Abbruch
  if (k == '*') {
    resetCodeEntry();

    if (uiState == UI_SET1) drawSetCode1Screen();
    else if (uiState == UI_SET2) drawSetCode2Screen();
    else if (uiState == UI_CODE) drawEnterCodeScreen();
    else if (uiState == UI_STATUS) drawStatusScreen();
    else if (uiState == UI_RESET_CONFIRM) drawResetConfirmScreen();
    else if (uiState == UI_ALARM) {
      // Alarm-Screen neu zeichnen (ohne Flackern warten)
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

  // STATUS: Shortcuts (nur wenn nicht im Alarm)
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
