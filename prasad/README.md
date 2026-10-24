# Automatic Prasad Dispenser V3 (ESP8266 + VL53L0X + N20 DC Motor)

A touchless, automatic prasad dispensing machine controller powered by an **ESP8266**, **VL53L0X Time-of-Flight distance sensor**, and an **N20 DC gearmotor** driven via an **AO3400 N-channel MOSFET**.

Features a responsive Wi-Fi Hotspot with a **Captive Portal Web UI**, continuous real-time distance radar, live hardware telemetry (motor & LED status), dual-distance hysteresis visualization, portion-time calibration, and permanent EEPROM persistence.

---

## 1. Key Features

- **Touchless Hand Detection & Continuous Mid-Dispense Monitoring**:
  - Millimeter-accurate distance measurement via VL53L0X I2C Time-of-Flight sensor.
  - Multi-sample debounce filter (~35ms per sample) prevents optical false triggers.
  - **Immediate Mid-Dispense Cutoff:** The sensor continuously checks for hand presence while dispensing. If confirmed that a devotee removed their hand mid-dispense (2 consecutive samples ~70ms beyond hysteresis reset point), **the motor cuts off immediately** to avoid wasting prasad.
- **Strict Anti-Abuse & Anti-Retrigger Guarantee**:
  - Devotee places hand within trigger distance &rarr; motor runs once for the exact configured duration &rarr; motor shuts OFF immediately.
  - **Even if the devotee keeps their hand under the sensor, the motor will NEVER run again.**
  - **Hand-Removal Triggered Cooldown**: The cooldown timer starts *only* after the devotee pulls their hand back beyond `Trigger Distance + Hysteresis`.
  - **Hand-Replace Cooldown Reset**: If the devotee places their hand back before the cooldown timer expires, the timer resets immediately and waits for hand removal again.
  - Only after the hand is kept away for the entire cooldown period does the machine re-arm to READY with the Status LED in full steady glow.
- **Dynamic Status LED Signaling**:
  - **Ready (Idle):** Full steady glow (Solid ON - signals machine is ready).
  - **Dispensing:** Fast energetic strobe blink (80ms ON / 80ms OFF) signaling active motor rotation.
  - **Waiting (Hand Removal):** Distinct alert blink (200ms ON / 200ms OFF) prompting devotee to remove hand.
  - **Waiting (Cooldown):** Gentle slow pulse (400ms ON / 400ms OFF) indicating cooldown timer active.
  - **Ready Again:** Returns to full steady glow.
- **Full Digital Torque Drive (Pure Digital - No PWM)**:
  - AO3400 MOSFET gate is driven at full saturation voltage (`digitalWrite(HIGH)`) for maximum starting torque on the N20 gearmotor through cereal dispenser silicone paddles.
  - PWM speed control has been completely removed to prevent gearmotor stalling and simplify portion control.
- **Wi-Fi Hotspot with Captive Portal**:
  - Built-in Access Point (`SSID: Prasad-Dispenser`, `Password: password123`).
  - Automatic captive portal detection redirects smartphones (Android, iOS, Windows) directly to the web dashboard upon connection.
  - Non-blocking server architecture with `yield()` calls ensures zero Wi-Fi lag and prevents ESP8266 Watchdog (WDT) resets.
- **Comprehensive Real-Time Feedback Web UI**:
  - **Live Distance Radar:** Continuous distance update in cm and mm with a 0–30 cm graphical progress bar.
  - **Dual Hysteresis Lines:** Visual markers on the distance bar showing both the 🔴 Trigger Point (`< 16.0 cm`) and 🔵 Reset Point (`> 18.0 cm`).
  - **Motor Running Badge:** Live spinning gear indicator displaying `RUNNING (X ms)` vs `STOPPED (OFF)`.
  - **Hardware LED Badge:** Real-time state of the onboard status LED (`LED ON (READY)` vs `LED OFF (WAITING)`).
  - **Live Cooldown Countdown:** Real-time countdown banner during the waiting pause between devotees.
  - **Portion Size Tuning:** Quick preset chips (`800ms`, `1000ms`, `1200ms`, `1500ms`, `1800ms`, `2000ms`) plus fine 50ms adjustments.
  - **Hand Settle Delay:** Preset chips (`0ms`, `50ms`, `100ms`, `150ms`, `200ms`, `300ms`) to let devotees settle hands before motor starts.
  - **Built-in Calibration Guide Card:** Embedded in the Web UI explaining every parameter, hysteresis, and portion tuning.
  - **Controls & Diagnostics:** Manual 1-dose test trigger, emergency cutoff, connected hotspot clients, free RAM, and auto-persisted serve counter.
  - **Self-Healing I2C Recovery:** Automatically recovers sensor bus from electrical motor back-EMF noise.

---

## 2. Hardware Connections & Pinout

### ESP8266 Pin Map

| Component | Pin | ESP8266 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **AO3400 MOSFET** | Gate (G) | **D5** (GPIO 14) | 100&Omega; series gate resistor; 10k&Omega; pulldown to GND |
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
| **Status LED** | External / Onboard | **D4** (GPIO 2) | External LED Anode to D4, Cathode to GND with 220&Omega; resistor (Active-HIGH: HIGH=ON) |

### Circuit Diagram

```
                 +-- External DC Motor Power Supply (5V - 6V)
                 |
                 +----------+--------------------+
                            |                    |
                            +                 [Cathode (Stripe)]
                       [N20 DC MOTOR]        [FLYBACK DIODE 1N5819/1N4007]
                            +                 [Anode]
                            |                    |
                            +----------+---------+
                                       |
                                    [DRAIN]
ESP8266 D5 (GPIO 14) ---[ 100 ohm ]--[GATE]  AO3400 N-MOSFET
                              |     [SOURCE]
                           [ 10k ]     |
                           Resistor    |
                              |        |
ESP8266 GND ------------------+--------+-------- Common Ground (External PSU GND)

ESP8266 D2 (GPIO 4) -------- VL53L0X SDA
ESP8266 D1 (GPIO 5) -------- VL53L0X SCL
ESP8266 3.3V --------------- VL53L0X VIN
ESP8266 GND ---------------- VL53L0X GND
```

---

## 3. Web UI & Configuration Parameters

### How to Connect
1. Connect your smartphone, tablet, or laptop to the Wi-Fi network:
   - **SSID:** `Prasad-Dispenser`
   - **Password:** `password123`
2. The **Captive Portal** popup will open automatically.
3. If it doesn't open automatically, open any browser and visit:
   - `http://192.168.4.1` or `http://prasad.local`
4. **Serial Monitor Baud Rate:** `74880` (Matches ESP8266 native bootloader rate).

### Configurable Parameters

| Parameter | Default | Range | Description |
| :--- | :--- | :--- | :--- |
| **Motor Run Duration** | `1800 ms` | 100 – 10,000 ms | Time the motor spins to advance one blade pocket dose |
| **Trigger Distance** | `16.0 cm` | 2.0 – 40.0 cm | Hand distance to initiate dispensing |
| **Removal Hysteresis** | `2.0 cm` | 0.5 – 15.0 cm | Extra distance hand must retreat (Reset at `Trigger + Hysteresis`) |
| **Cooldown Time** | `1500 ms` | 200 – 5,000 ms | Mandatory waiting pause between consecutive devotees |
| **Hand Settle Delay** | `100 ms` | 0 – 3,000 ms | Delay after hand detection before motor starts (time to settle hands) |
| **Debounce Samples** | `3 samples` | 1 – 10 samples | Consecutive valid sensor samples needed (~35ms per sample) |

Click **`SAVE SETTINGS TO EEPROM`** to save your calibration permanently to the ESP8266 flash memory.

---

## 4. Arduino IDE Setup & Installation

### Board Package & Settings
- Board: **NodeMCU 1.0 (ESP-12E Module)** or **Generic ESP8266 Module**
- CPU Frequency: **80 MHz** (or 160 MHz)
- Flash Size: **4MB**
- Upload Speed: **115200** or **921600**
- **Serial Monitor Baud Rate:** `74880`

### Required Libraries
- **VL53L0X** by *Pololu* (install via Tools &rarr; Manage Libraries)
