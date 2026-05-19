# Arduino Alarm System

A code-protected alarm system based on an **Arduino Micro** using  
a TFT display, keypad (TP229), four photoelectric sensors, a siren and an alarm LED.

This project is designed for industrial or semi‑industrial use cases where
reliable alarm handling and user authentication are required.

---

## 🔐 Features

- Arm / disarm alarm using a 4‑digit code
- Alarm latch (alarm stays active until acknowledged)
- Siren and LED activation on alarm
- Red blinking TFT display during alarm condition
- Code-protected alarm acknowledgment
- Persistent storage using EEPROM
- Factory reset and code change supported

---

## 🧠 System Overview

**Controller**
- Arduino Micro (single controller only)

**Inputs**
- 4 × photoelectric sensors (LIGHT BARRIERS)
- 1 × TP229 keypad (2‑wire interface)

**Outputs**
- Active siren
- Red alarm LED

**Display**
- 1.44" TFT Display (ST7735S, SPI interface)

---

## 🔌 Pin Mapping

### TFT Display (ST7735S)

| Signal | Arduino Micro Pin |
|------|-------------------|
| CS   | 6 |
| DC   | 7 |
| RST  | 8 |
| MOSI | 16 |
| SCK  | 15 |
| VCC  | 5V |
| GND  | GND |

---

### Keypad (TP229)

| Signal | Arduino Micro Pin |
|------|-------------------|
| SCL  | 2 |
| SDA  | 3 |
| VCC  | 5V |
| GND  | GND |

---

### Sensors & Actuators

| Device | Pin |
|------|-----|
| Photoelectric Sensor 1 | 5 |
| Photoelectric Sensor 2 | 4 |
| Photoelectric Sensor 3 | 11 |
| Photoelectric Sensor 4 | 10 |
| Siren | 9 |
| Alarm LED | 12 |

⚠️ **Always use a resistor (220–330 Ω) with the LED**

---

## 🚨 Alarm Behavior

1. Alarm is **armed**
2. Any sensor is triggered
3. Siren and alarm LED turn ON
4. TFT display blinks red
5. Alarm remains latched
6. Alarm can ONLY be acknowledged using the valid code
7. Acknowledgment disarms the alarm automatically

---

## 🧪 Safety Notes

- Alarm can never be stopped without entering the correct code
- Alarm status and code are stored in EEPROM
- Power loss does not reset the alarm state
- All modules must share a common GND

---

## 📦 Hardware Requirements

- Arduino Micro
- 1.44" TFT Display (ST7735S)
- TP229 Keypad (12‑key)
- 4 × photoelectric sensors (5V)
- Active 5V siren
- Red LED + resistor
- Jumper wires / connectors
- Enclosure (recommended)

---

## 🛠 Recommended Startup Order

1. Arduino + TFT display
2. Keypad test
3. LED test
4. Siren test
5. Sensors one by one
6. Full system test

---

## 📄 License

MIT License

You are free to use, modify and distribute this project  
for personal or commercial purposes.

---

## ✅ Project Status

✔ Functional  
✔ Tested  
✔ Documentation complete  
✔ Ready for deployment