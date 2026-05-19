# 📘 Arduino Alarm System – Project Documentation

---

## 📑 Table of Contents

1. Project Overview  
2. System Architecture  
3. Hardware Layout  
4. Functional Description  
5. Signal Flow  
6. State Machine  
7. Software Design  
8. Safety Considerations  
9. Installation & Commissioning  
10. Maintenance  
11. Bill of Materials  

---

## 1. Project Overview

This project is a **code-protected alarm system** based on an **Arduino Micro**.

The system monitors multiple photoelectric sensors.  
When the system is armed and a sensor is triggered:

- A siren is activated  
- A warning LED turns on  
- The TFT display flashes red  

The alarm remains active (**latched**) until acknowledged using a valid code.

---

## 2. System Architecture

### 🧠 Block Diagram