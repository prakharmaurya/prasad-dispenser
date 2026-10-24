# Automatic Prasad Dispenser V3 — Firmware & Electronics

This directory contains the embedded C++ source code (`prasad.ino`) and electrical documentation for the **NodeMCU ESP8266 + VL53L0X Time-of-Flight + AO3400 MOSFET + N20 DC Motor** controller.

---

## 1. Quick File Map

- **[`prasad.ino`](file:///c:/Users/Excel/Desktop/Prasad/prasad/prasad.ino)**: Complete Arduino sketch featuring non-blocking state machine, VL53L0X ToF optical distance measurement, Wi-Fi Access Point with Captive Portal, non-blocking HTTP web server, live telemetry radar, dual-distance hysteresis visualization, portion-time calibration, and EEPROM persistence.
- **[`WIRING_AND_SETUP.md`](file:///c:/Users/Excel/Desktop/Prasad/prasad/WIRING_AND_SETUP.md)**: Hardware pinout table, circuit diagram, motor driver notes, anti-retrigger logic explanation, and calibration instructions.
- **Master Documentation**: For full end-to-end mechanical, electrical, and CAD documentation, see the [Master Project README](file:///c:/Users/Excel/Desktop/Prasad/README.md).

---

## 2. Hardware Pinout Reference

| Component | Pin | ESP8266 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **AO3400 MOSFET** | Gate (G) | **D5** (GPIO 14) | 100&Omega; series resistor; 10k&Omega; pulldown to GND |
| **AO3400 MOSFET** | Source (S) | **GND** | Connect to common ground |
| **AO3400 MOSFET** | Drain (D) | **Motor (-)** | Switched ground connection to motor |
| **N20 DC Motor** | (+) | **Ext Power (+)** | 5V / 6V external supply |
| **N20 DC Motor** | (-) | **AO3400 Drain** | Across flyback diode |
| **Flyback Diode** | Cathode (Stripe) | **Motor (+)** | 1N5819 Schottky or 1N4007 (MANDATORY) |
| **Flyback Diode** | Anode | **Motor (-)** | Absorbs inductive back-EMF spikes |
| **VL53L0X Sensor** | SDA | **D2** (GPIO 4) | I2C Data line |
| **VL53L0X Sensor** | SCL | **D1** (GPIO 5) | I2C Clock line |
| **VL53L0X Sensor** | VIN / VCC | **3.3V** | Power from ESP8266 3.3V pin |
| **VL53L0X Sensor** | GND | **GND** | Common ground |
| **Status LED** | External / Onboard | **D4** (GPIO 2) | External LED Anode to D4, Cathode to GND with 220&Omega; resistor |

---

## 3. Wi-Fi Access Point & Web UI

- **Hotspot Name (SSID):** `Prasad-Dispenser`
- **Password:** `password123`
- **Web Dashboard URL:** `http://192.168.4.1` or `http://prasad.local`
- **Serial Monitor Baud:** `74880`
