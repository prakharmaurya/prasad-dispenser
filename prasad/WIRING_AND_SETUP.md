# Automatic Prasad Dispenser V3 (ESP8266 + VL53L0X + N20 Motor)

## 1. Hardware Pin Connections

| Component | Pin | ESP8266 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **AO3400 MOSFET** | Gate (G) | **D5** (GPIO 14) | 100&Omega; series resistor; 10k&Omega; pulldown to GND |
| **AO3400 MOSFET** | Source (S) | **GND** | Connect to common ground |
| **AO3400 MOSFET** | Drain (D) | **Motor (-)** | Switched ground connection to motor |
| **N20 DC Motor** | (+) | **Ext 5V/6V (+)** | Connect to external motor power source |
| **N20 DC Motor** | (-) | **AO3400 Drain** | Connect across flyback diode |
| **Flyback Diode** | Cathode (stripe) | **Motor (+)** | 1N4007 or 1N5819 Schottky (MANDATORY) |
| **Flyback Diode** | Anode | **Motor (-)** | Protects MOSFET from inductive back-EMF spike |
| **VL53L0X Sensor** | VCC / VIN | **3.3V** | Power from ESP8266 3.3V pin |
| **VL53L0X Sensor** | GND | **GND** | Common ground |
| **VL53L0X Sensor** | SDA | **D2** (GPIO 4) | I2C Data line |
| **VL53L0X Sensor** | SCL | **D1** (GPIO 5) | I2C Clock line |
| **Status LED** | External / Onboard | **D4** (GPIO 2) | External LED Anode to D4, Cathode to GND with 220&Omega; resistor (Active-HIGH: HIGH=ON) |

---

## 2. Circuit Diagram

```
                 +-- External DC Motor Power (5V - 6V)
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

## 3. How the Dispensing Logic Works (Portioning & Anti-Retrigger)

1. **Pure Digital Full-Torque Drive (NO PWM)**:
   - D5 drives the AO3400 gate at full 100% saturation voltage (`digitalWrite(PIN_MOTOR, HIGH)`). This provides maximum starting torque to the N20 gearmotor through the cereal dispenser mechanism without any PWM stutter or torque loss.
   - PWM speed control has been completely removed from both firmware and UI because N20 motors need full torque to turn silicone dispenser blades reliably.

2. **Calibrating Rotor Blade Passes via Motor Run Duration**:
   - Portion control is handled via the precise **Motor Run Duration (ms)** setting in the Web UI.
   - Example tuning:
     - If the rotor has 6 blades (60° per dose) and takes ~1000ms to advance one blade pocket at full speed, set duration to **1000ms**.
     - Quick chips in the UI let you test and tune at **600ms, 800ms, 1000ms, 1200ms, 1500ms, 2000ms**, or adjust in fine 50ms increments.

3. **Guaranteed Single-Dispense & Continuous Hand Monitoring**:
   - **`STATE_IDLE` (READY)**: Devotee places hand within trigger distance (default `< 16.0 cm`).
     - **Status LED:** Steady Solid Glow (signals machine is ready).
     - Once debounce samples pass:
       - If `Hand Settle Delay > 0` (default `100 ms`), system enters **`STATE_SETTLE`** allowing devotee time to settle hands inside the machine.
       - If hand is removed during settle delay, the cycle safely aborts back to `STATE_IDLE`.
   - **`STATE_SETTLE`**: Pauses for configured delay (e.g. 100ms) with motor OFF while hand is settled. Once delay elapses, motor turns ON and enters `STATE_DISPENSING`.
   - **`STATE_DISPENSING`**: Motor runs to advance the rotor by 1 dose (default `1800 ms`).
     - **Status LED:** Fast Energetic Strobe Blink (80ms ON / 80ms OFF) signaling active motor dispensing.
     - **Continuous Hand Check:** The VL53L0X sensor continuously monitors hand presence during motor operation.
     - **Immediate Mid-Dispense Cutoff:** If confirmed that the devotee withdrew their hand mid-dispense (2 consecutive samples ~70ms beyond hysteresis threshold), **the motor stops immediately** and transitions to `STATE_COOLDOWN` to prevent prasad spilling onto the floor!
   - **`STATE_WAIT_FOR_REMOVAL`**: **Motor is completely OFF.**
     - **Status LED:** Distinct Alert Blink (200ms ON / 200ms OFF) signaling devotee to remove hand.
     - If the devotee **leaves their hand in place**, the motor **NEVER** runs again and cooldown timer does not start.
     - The cooldown timer **only starts** once the devotee pulls their hand back past `Trigger Distance + Hysteresis` (default `> 18.0 cm`).
   - **`STATE_COOLDOWN`**: Cooldown countdown runs (default `1500 ms`).
     - **Status LED:** Slow Calm Pulse (400ms ON / 400ms OFF) signaling cooldown pause in progress.
     - If the devotee **places their hand back** before cooldown finishes, cooldown is **immediately reset**, transitioning back to `STATE_WAIT_FOR_REMOVAL`.
     - Once devotee keeps hand away continuously for full cooldown delay, system resets to **`STATE_IDLE`** and Status LED returns to **Full Steady Glow: "Ready for next devotee"**.

---

## 4. Web UI & Captive Portal

- **Hotspot Name (SSID):** `Prasad-Dispenser`
- **Password:** `password123`
- **Web Address:** `http://192.168.4.1` or `http://prasad.local`
- **UART Serial Monitor Baud:** `74880` (Matches ESP8266 native crystal bootloader baud)
- **Captive Portal:** Automatically opens when your phone connects to the Wi-Fi.
- **Controls & Real-Time Feedback:**
  - **Live Continuous Distance Radar:** Real-time distance measurement in cm and mm with a 0–30 cm graphical progress bar.
  - **Dual Hysteresis Lines:** Visual markers on the bar showing 🔴 Trigger Point (`< 16.0 cm`) and 🔵 Reset Point (`> 18.0 cm`).
  - **Live Motor Feedback Badge:** Animated spinning gear indicator (`RUNNING (X ms)` vs `STOPPED (OFF)`).
  - **Live Status LED Badge:** Real-time hardware status of the LED (`LED ON (READY)` vs `LED OFF (WAITING)`).
  - **Live Cooldown Countdown Banner:** Active countdown timer displaying milliseconds remaining before ready.
  - **Motor Run Duration (ms):** Easily tuned with quick preset buttons (default `1800ms`).
  - **Hand Settle Delay (ms):** Settling pause before dispensing (default `100ms`).
  - **Hand Detection Distance (cm):** Trigger threshold (default `16.0cm`).
  - **Removal Hysteresis Gap (cm):** Reset distance offset (default `2.0cm`, reset at `18.0cm`).
  - **Cooldown (ms):** Anti-abuse pause between devotees (default `1500ms`).
  - **Test Dispense Now:** Manual 1-dose test trigger button.
  - **Emergency Motor Cutoff:** Instant safety motor cutoff.
  - **Save Settings to EEPROM:** Saves values permanently into flash memory.
