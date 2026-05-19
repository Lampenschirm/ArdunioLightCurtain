# 📘 Arduino Alarm System
## Final Project Documentation (with Diagrams)

---

## 🧾 Table of Contents

1. Project Overview  
2. System Architecture  
3. Hardware & Wiring  
4. Functional Behavior  
5. Signal Flow  
6. State Machine  
7. EEPROM Data  
8. Installation & Commissioning  
9. Maintenance & Troubleshooting  
10. Bill of Materials  

---

## 1. Project Overview

The Arduino Alarm System is a **code-protected security system** based on an Arduino Micro.

When the system is armed and a sensor is triggered:
- Siren activates  
- Alarm LED turns ON  
- TFT display flashes RED  

The alarm is **latched** and remains active until a valid code is entered.

---

## 2. System Architecture

### Block Diagram

![Block Diagram](docs/diagram_block.png)

**Description:**
- Arduino Micro is the central controller  
- Sensors trigger alarm conditions  
- Keypad provides user input  
- Outputs: siren + LED  
- TFT display visualizes system state  

---

## 3. Hardware & Wiring

### Wiring Diagram

![Wiring Diagram](docs/diagram_wiring.png)

### Pin Mapping

| Subsystem | Signal | Pin |
|----------|-------|----|
| TFT | CS | 6 |
| TFT | DC | 7 |
| TFT | RST | 8 |
| TFT | MOSI | 16 |
| TFT | SCK | 15 |
| Keypad | SCL | 2 |
| Keypad | SDA | 3 |
| Sensor | LS1 | 5 |
| Sensor | LS2 | 4 |
| Sensor | LS3 | 11 |
| Sensor | LS4 | 10 |
| Output | Siren | 9 |
| Output | LED | 12 |

---

## 4. Functional Behavior

### Modes

- **Disarmed**
  - Sensors inactive
- **Armed**
  - Sensors monitored
- **Alarm**
  - Latched state
  - Requires code acknowledgment

---

### Alarm Sequence

1. Sensor triggered  
2. Alarm is latched  
3. Siren ON  
4. LED ON  
5. TFT flashes RED  
6. Wait for code  

---

## 5. Signal Flow

![Signal Flow](docs/diagram_signal_flow.png)

### Description

``
---

## 6. State Machine

![State Machine](docs/diagram_state_machine.png)

### Flow

---

## 7. EEPROM Data

| Address | Data |
|--------|-----|
| 0 | Magic byte |
| 1–4 | User Code |
| 5 | Alarm state |

---

## 8. Installation & Commissioning

### Startup Procedure

1. Connect hardware
2. Power system
3. Set 4-digit code
4. Test keypad
5. Test sensors
6. Trigger alarm
7. Verify reset via code

---

## 9. Maintenance & Troubleshooting

### Common Issues

- No display → check SPI + power  
- Sensor always triggered → check INPUT_PULLUP  
- Siren not stopping → check code logic  
- LED not working → check resistor  

---

## 10. Bill of Materials (BOM)

| Component | Quantity |
|----------|--------|
| Arduino Micro | 1 |
| TFT Display ST7735S | 1 |
| TP229 Keypad | 1 |
| Sensors | 4 |
| Siren | 1 |
| LED + resistor | 1 |

---

## ✅ Project Status

✔ Functional  
✔ Tested  
✔ Documented  
✔ Deployment ready  

---

