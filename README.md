# 🕉️ Automatic Prasad Dispenser V3 (Touchless Smart Temple & Mandir Dispenser)

An all-in-one, open-source, touchless automatic prasad dispensing machine combining a **custom 3D-printed 4.13:1 gear-reduced dispenser chassis**, an **ESP8266 Wi-Fi microcontroller**, an **STMicroelectronics VL53L0X Time-of-Flight optical distance sensor**, and an **N20 DC gearmotor** driven via an **AO3400 N-channel MOSFET**.

Designed for temples, ashrams, cultural centers, community feasts (*bhandara / langar*), and home mandirs to dispense sacred dry offerings (*mishri, batasha, dry fruits, roasted chickpeas, boondi, laddoos, pedas*) completely hands-free, hygienically, and with zero food waste.

---

## 📑 Table of Contents

1. [System Architecture & Working Principle](#1-system-architecture--working-principle)
2. [Complete Bill of Materials (BOM)](#2-complete-bill-of-materials-bom)
3. [Mechanical Design & 3D Printing (CAD)](#3-mechanical-design--3d-printing-cad)
4. [Step-by-Step Mechanical Assembly](#4-step-by-step-mechanical-assembly)
5. [Electronic Pinout & Circuit Schematic](#5-electronic-pinout--circuit-schematic)
6. [Firmware Architecture & Dispense Logic](#6-firmware-architecture--dispense-logic)
7. [Wi-Fi Captive Portal & Web Dashboard](#7-wi-fi-captive-portal--web-dashboard)
8. [Arduino IDE Flashing & Setup](#8-arduino-ide-flashing--setup)
9. [Food Safety, Sanitization & Maintenance](#9-food-safety-sanitization--maintenance)
10. [Troubleshooting Guide](#10-troubleshooting-guide)

---

## 1. System Architecture & Working Principle

```
                            [ CLEAR PRASAD HOPPER / RESERVOIR ]
                                             |
                                             v
     +---------------------------------------------------------------------------------+
     |                               MECHANICAL CHASSIS                                |
     |                                                                                 |
     |  +--------------------+        Meshes (4.13:1)       +-----------------------+  |
     |  | N20 Micro Motor    |=============================>| 63mm Main Spur Gear   |  |
     |  | (6V, 60-100 RPM)   |   Pinion (15.27mm, 28T)      | (63.15mm, 120T, 0.5M) |  |
     |  +--------------------+                              +-----------------------+  |
     |            ^                                                     |              |
     |            | Full Digital 100% Saturation Drive                  v              |
     |  +--------------------+                              +-----------------------+  |
     |  | AO3400 N-MOSFET    |                              | Main 8mm Drive Shaft  |  |
     |  | + Flyback Diode    |                              +-----------------------+  |
     |  +--------------------+                                          |              |
     |            ^                                                     v              |
     |            | D5 (GPIO 14)                            +-----------------------+  |
     |  +--------------------+                              | 4-Blade Dosing Rotor  |  |
     |  | NodeMCU ESP8266    |                              | (Meters 90° per dose) |  |
     |  | Microcontroller    |                              +-----------------------+  |
     |  +--------------------+                                          |              |
     |     |              ^                                             v              |
     |     v              | I2C (D1/D2)                     +-----------------------+  |
     |  [Status LED]   [VL53L0X ToF Sensor] <-------------> | Dispense Nozzle Spout |  |
     |  (D4, Patterns) (Downward Optical Beam)              +-----------------------+  |
     +---------------------------------------------------------------------------------+
                                                                        |
                                                                        v
                                                            [ DEVOTEE'S CUPPED HANDS ]
```

### The Dispensing Cycle
1. **Approach & Detection:** A devotee places their cupped hands (*anjali mudra*) beneath the dispensing nozzle.
2. **Optical Measurement:** The STMicroelectronics **VL53L0X Time-of-Flight sensor** emits invisible infrared laser pulses to calculate hand distance with millimeter accuracy (default trigger: `< 16.0 cm`).
3. **Multi-Sample Debounce & Settle Delay:** The sensor checks multiple consecutive samples (~35ms each) to eliminate optical flicker, then pauses for a configurable **Hand Settle Delay** (default `100 ms`) so the devotee has settled their hands comfortably.
4. **Torque-Multiplied Dispensing:** The ESP8266 drives the **AO3400 MOSFET** into full saturation. The N20 micro gearmotor and 4.13:1 gear train rotate the 4-blade rotor by 90°, smoothly discharging one portion through the funnel nozzle directly into the devotee's hands.
5. **Immediate Mid-Dispense Cutoff:** If the devotee pulls their hand away mid-dispense, the sensor detects hand absence within ~70ms and **cuts motor power immediately** to stop prasad from falling onto the floor.
6. **Anti-Abuse Lockout:** After dispensing, the motor shuts off. Even if the devotee leaves their hand in place, the machine **NEVER retriggers**. The cooldown timer only arms once the devotee removes their hand past `Trigger + Hysteresis` (default `> 18.0 cm`).

---

## 2. Complete Bill of Materials (BOM)

### Mechanical & 3D Printed Parts

| Component | Quantity | Slicer / Source File | Material | Notes |
| :--- | :---: | :--- | :--- | :--- |
| **Top Chassis Enclosure** | 1 | [`cad/body top.stl`](file:///c:/Users/Excel/Desktop/Prasad/cad/body%20top.stl) | Food-Grade PETG / PLA | Plate 1 (Hopper receiver & gear cover) |
| **Bottom Base Enclosure** | 1 | [`cad/body_bottom.stl`](file:///c:/Users/Excel/Desktop/Prasad/cad/body_bottom.stl) | PETG / Tough PLA | Plate 2 (Motor bay, shaft bosses, electronics) |
| **Main Spur Gear (120T)** | 1 | [`cad/main gear.stl`](file:///c:/Users/Excel/Desktop/Prasad/cad/main%20gear.stl) | PETG / Tough PLA | Plate 3 (63.15mm OD, 8mm D-bore) |
| **Motor Pinion Gear (28T)** | 1 | [`cad/motor_gear.stl`](file:///c:/Users/Excel/Desktop/Prasad/cad/motor_gear.stl) | PETG / Tough PLA | Plate 3 (15.27mm OD, 3mm D-bore) |
| **4-Blade Dispenser Rotor** | 1 | [`cad/main rotor.stl`](file:///c:/Users/Excel/Desktop/Prasad/cad/main%20rotor.stl) | Food-Grade PETG / TPU | Plate 4 (51.4mm standard) or Plate 7 (49.2mm) |
| **Dispensing Funnel Nozzle** | 1 | [`cad/nozzle.stl`](file:///c:/Users/Excel/Desktop/Prasad/cad/nozzle.stl) | Food-Grade PETG | Plate 5 (Funnel chute + downward ToF mount) |
| **Main Drive Shaft (101mm)** | 1 | [`cad/main_axis.stl`](file:///c:/Users/Excel/Desktop/Prasad/cad/main_axis.stl) | PETG (Solid) | Plate 6 (Hardened 8mm shaft with stop collar) |
| **Motor Clamping Cover** | 1 | [`cad/motor_cover.stl`](file:///c:/Users/Excel/Desktop/Prasad/cad/motor_cover.stl) | PETG / PLA | Plate 6 (Retainer bracket for N20 motor) |
| **N20 DC Micro Gearmotor** | 1 | Off-the-shelf | Metal Gearbox | 6V DC, 60–100 RPM, 3mm D-shaft |
| **M3 Screws & Hardware** | 10 | M3 × 8mm, 12mm, 16mm | Stainless Steel | Chassis & motor mounting |
| **M3 Heat-Set Threaded Inserts** | 10 | M3 × 4mm (OD 4.2mm) | Brass | Heat-pressed into chassis boss holes |

### Electronics & Sensors

| Component | Quantity | Key Specifications | Purpose |
| :--- | :---: | :--- | :--- |
| **NodeMCU ESP8266** | 1 | ESP-12E / D1 Mini (80MHz/160MHz) | Wi-Fi hotspot, captive portal, state machine |
| **ST VL53L0X Sensor** | 1 | I2C Time-of-Flight (940nm VCSEL) | Millimeter-accurate optical distance detection |
| **AO3400 N-MOSFET** | 1 | 30V / 5.7A, $R_{DS(ON)} \approx 28\text{m}\Omega$ | Full-saturation digital ground switch for motor |
| **Flyback Diode** | 1 | 1N5819 Schottky or 1N4007 | Suppresses inductive voltage spikes across motor |
| **Gate Resistors** | 2 | 100&Omega; series; 10k&Omega; pulldown to GND | Protects ESP8266 pin & prevents startup motor spin |
| **Status LED** | 1 | 5mm Diffused LED (Amber/Green) + 220&Omega; | Multi-pattern visual state indicator |
| **Power Supply** | 1 | 5V / 6V 2A DC Adapter (or 18650 Li-ion) | Main power for ESP8266 and N20 motor |

---

## 3. Mechanical Design & 3D Printing (CAD)

All CAD assets, 3D printing project files, and interactive tools are located in the **[`cad/`](file:///c:/Users/Excel/Desktop/Prasad/cad/)** directory:

- **Complete 3MF Slicer Project:** [`cad/prasad_v3.1.3mf`](file:///c:/Users/Excel/Desktop/Prasad/cad/prasad_v3.1.3mf) pre-configured with all 7 plates for Bambu Lab A1 / OrcaSlicer.
- **Interactive 3D WebGL Viewer:** Open **[`cad/viewer.html`](file:///c:/Users/Excel/Desktop/Prasad/cad/viewer.html)** in any browser to inspect the 3D assembly, test the exploded view slider, inspect individual parts, and drag-and-drop custom STLs.
- **Parametric OpenSCAD Generator:** Use **[`cad/prasad_parametric_generator.scad`](file:///c:/Users/Excel/Desktop/Prasad/cad/prasad_parametric_generator.scad)** to customize rotor blade count (3, 4, 6 blades), nozzle exit aperture, gear teeth, or shaft diameters in OpenSCAD.
- **Dedicated CAD Manual:** Detailed printing parameters, layer heights, and gear mechanics are documented in **[`cad/README.md`](file:///c:/Users/Excel/Desktop/Prasad/cad/README.md)**.

### Slicer Print Parameters

| Setting | Recommended Value | Notes |
| :--- | :--- | :--- |
| **Layer Height** | `0.20 mm` | `0.16 mm` recommended for gear teeth |
| **Wall Loops** | `3 to 4 walls` | High strength required for gear teeth and shaft |
| **Infill** | `15% – 25% Gyroid` | Gyroid infill provides isotropic mechanical strength |
| **Material (Food Contact)** | **Food-Grade PETG** | Clean, non-porous, dishwasher safe under 65°C |
| **Material (Flexible Blades)** | **TPU 95A** (Optional) | For soft paddle blades that never crush dry sweets |

---

## 4. Step-by-Step Mechanical Assembly

```
         STEP 1: Brass Inserts & Deburr
                 |
                 v
         STEP 2: N20 Motor & Pinion Gear (Plate 3 & 6)
                 - Slide 15.27mm pinion gear onto 3mm motor D-shaft
                 - Fasten motor into body_bottom using motor_cover (4× M3 screws)
                 |
                 v
         STEP 3: Main Drive Shaft & Spur Gear (Plate 3 & 6)
                 - Insert 8mm main_axis shaft through chassis bearing bosses
                 - Slide 63.15mm spur gear onto D-flat, meshing with pinion (4.13:1 reduction)
                 |
                 v
         STEP 4: Dispenser Chamber & 4-Blade Rotor (Plate 4)
                 - Slide 4-blade rotor onto shaft inside the food chamber
                 |
                 v
         STEP 5: Dispensing Nozzle & VL53L0X ToF Sensor (Plate 5)
                 - Screw VL53L0X sensor into nozzle bracket pointing downwards
                 - Fasten nozzle to bottom discharge chute (2× M3 screws)
                 |
                 v
         STEP 6: Electronics Bay & Enclosure Closure (Plate 1)
                 - Wire ESP8266, MOSFET, and LED inside body_bottom wiring bay
                 - Lower body top over chassis and fasten with 4× M3 screws
                 - Mount clear prasad hopper cylinder into top receiver collar
```

---

## 5. Electronic Pinout & Circuit Schematic

### ESP8266 Pin Map

| Component | Pin | ESP8266 NodeMCU | Wire Color | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **AO3400 MOSFET** | Gate (G) | **D5** (GPIO 14) | Yellow | 100&Omega; series resistor; 10k&Omega; pulldown to GND |
| **AO3400 MOSFET** | Source (S) | **GND** | Black | Connect to common system ground |
| **AO3400 MOSFET** | Drain (D) | **Motor (-)** | Blue | Switched ground connection to motor |
| **N20 DC Motor** | (+) | **Ext Power (+)** | Red | Connect to 5V / 6V supply rail |
| **N20 DC Motor** | (-) | **AO3400 Drain** | Blue | Across flyback diode anode |
| **Flyback Diode** | Cathode (Stripe) | **Motor (+)** | — | 1N5819 Schottky or 1N4007 (MANDATORY) |
| **Flyback Diode** | Anode | **Motor (-)** | — | Absorbs inductive back-EMF spikes |
| **VL53L0X Sensor** | SDA | **D2** (GPIO 4) | Green | I2C Data line |
| **VL53L0X Sensor** | SCL | **D1** (GPIO 5) | White | I2C Clock line |
| **VL53L0X Sensor** | VIN / VCC | **3.3V** | Orange | Power from ESP8266 3.3V pin |
| **VL53L0X Sensor** | GND | **GND** | Black | Common ground |
| **Status LED** | External Anode | **D4** (GPIO 2) | Purple | In series with 220&Omega; resistor to GND (Active-HIGH) |

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
ESP8266 D4 (GPIO 2) --------[ 220 ohm ]---> [Status LED Anode] ---> GND
```

---

## 6. Firmware Architecture & Dispense Logic

The firmware sketch is located at **[`prasad/prasad.ino`](file:///c:/Users/Excel/Desktop/Prasad/prasad/prasad.ino)**. It uses a non-blocking asynchronous state machine running on the ESP8266:

```
  +--------------------------------------------------------------------------------+
  |                                  STATE_IDLE                                    |
  |  - LED: Solid Steady ON ("Ready for Devotee")                                  |
  |  - Motor: OFF                                                                  |
  |  - Distance sensor polling (~35ms). When distance < Trigger Distance:         |
  +--------------------------------------------------------------------------------+
                                          |
                                          v (Debounce verified)
  +--------------------------------------------------------------------------------+
  |                                 STATE_SETTLE                                   |
  |  - LED: Fast subtle pulse (60ms)                                               |
  |  - Pauses for configured Hand Settle Delay (e.g. 100ms) with motor OFF          |
  |  - If devotee pulls hand away prematurely, cycle aborts back to STATE_IDLE     |
  +--------------------------------------------------------------------------------+
                                          |
                                          v (Settle delay elapses)
  +--------------------------------------------------------------------------------+
  |                               STATE_DISPENSING                                 |
  |  - LED: Energetic strobe blink (80ms ON / 80ms OFF)                            |
  |  - Motor: Full 100% digital saturation ON                                      |
  |  - VL53L0X continuously monitors hand in real-time mid-dispense:               |
  |    * If hand confirmed removed (~70ms) -> MOTOR CUTS OFF INSTANTLY!            |
  |    * If dispense duration (e.g. 1800ms) completes -> Motor cuts OFF           |
  +--------------------------------------------------------------------------------+
                                          |
                        +-----------------+-----------------+
                        | (Hand still placed)               | (Hand already pulled away)
                        v                                   v
  +-------------------------------------------+   +--------------------------------+
  |           STATE_WAIT_FOR_REMOVAL          |   |         STATE_COOLDOWN         |
  |  - LED: Alert blink (200ms ON / 200ms OFF)|   |  - LED: Calm pulse (400ms)     |
  |  - Prompts devotee to remove hands        |   |  - Cooldown timer running down |
  |  - Motor NEVER retriggers                 |   |  - If hand replaced, timer     |
  |  - Cooldown timer ONLY starts once hand   |   |    resets immediately          |
  |    retreats past Trigger + Hysteresis     |   +--------------------------------+
  +-------------------------------------------+                   |
                        |                                         v (Cooldown finished)
                        +-----------------------------------> [ STATE_IDLE ]
```

### Visual LED Signaling Patterns

| Dispenser State | Visual Pattern | Meaning to Devotee |
| :--- | :--- | :--- |
| **`STATE_IDLE`** | **Solid Steady Glow** | Machine is READY for next devotee |
| **`STATE_SETTLE`** | **Quick Tick Pulse (60ms)** | Hand recognized; settling hands |
| **`STATE_DISPENSING`** | **Energetic Strobe (80ms ON/OFF)** | Active dispensing; holding hands cupped |
| **`STATE_WAIT_FOR_REMOVAL`** | **Distinct Alert Blink (200ms ON/OFF)** | Dispense finished; please remove hands |
| **`STATE_COOLDOWN`** | **Gentle Slow Pulse (400ms ON/OFF)** | Anti-abuse cooldown in progress |

---

## 7. Wi-Fi Captive Portal & Web Dashboard

The machine broadcasts an Access Point with an automatic captive portal. No mobile app or internet connection is required.

### Connecting to the Dashboard
1. Connect your smartphone, tablet, or laptop to the Wi-Fi network:
   - **SSID:** `Prasad-Dispenser`
   - **Password:** `password123`
2. The **Captive Portal popup** opens automatically on Android, iOS, macOS, and Windows.
3. If it doesn't open automatically, visit **`http://192.168.4.1`** or **`http://prasad.local`**.
4. **Serial Monitor Baud Rate:** `74880` (native ESP8266 bootloader baud rate).

### Dashboard Telemetry & Controls
- **Live Distance Radar:** Continuous real-time distance in cm and mm with a 0–30 cm graphical bar.
- **Dual Hysteresis Lines:** Visual markers showing the 🔴 **Trigger Point** (`< 16.0 cm`) and 🔵 **Reset Point** (`> 18.0 cm`).
- **Live Motor Badge:** Animated spinning gear indicator showing `RUNNING (X ms)` vs `STOPPED (OFF)`.
- **Live Hardware LED Badge:** Real-time state of the physical LED (`LED ON` vs `LED OFF`).
- **Live Cooldown Countdown:** Real-time countdown banner during the waiting pause between devotees.
- **Portion Size Tuning:** Quick preset chips (`800ms`, `1000ms`, `1200ms`, `1500ms`, `1800ms`, `2000ms`) plus fine 50ms adjustment buttons.
- **Hand Settle Delay:** Preset chips (`0ms`, `50ms`, `100ms`, `150ms`, `200ms`, `300ms`).
- **Controls & Diagnostics:** Manual 1-dose test button, emergency motor cutoff, connected client counter, free heap memory, and cumulative serve counter saved to EEPROM.

---

## 8. Arduino IDE Flashing & Setup

### Board Package & Settings
- **Board:** `NodeMCU 1.0 (ESP-12E Module)` or `Generic ESP8266 Module`
- **CPU Frequency:** `80 MHz` (or `160 MHz`)
- **Flash Size:** `4MB (FS: 2MB / OTA: ~1019KB)`
- **Upload Speed:** `115200` or `921600`
- **Serial Monitor Baud:** `74880`

### Required Libraries
Install the following library via Arduino IDE **Tools &rarr; Manage Libraries**:
- **VL53L0X** by *Pololu* (official ST VL53L0X ToF driver)

*(Standard included libraries: `ESP8266WiFi`, `ESP8266WebServer`, `DNSServer`, `ESP8266mDNS`, `Wire`, `EEPROM`)*

---

## 9. Food Safety, Sanitization & Maintenance

1. **Food-Grade Contact Surfaces:** Always 3D-print `main rotor.stl` and `nozzle.stl` with certified virgin **Food-Grade PETG** or certified PLA.
2. **Smooth Flow Passages:** Print the interior walls of `nozzle.stl` at 0.16mm layer height to prevent grain dust from catching in layer grooves.
3. **Disassembly & Washing:**
   - Remove the 2× M3 screws securing `nozzle.stl` to wash the funnel under warm running water with mild dish soap.
   - Gently wipe the silicone/PETG rotor blades with a damp cloth.
   - *Never submerge the ESP8266, VL53L0X optical sensor, or N20 motor in water.*
4. **Moisture Prevention:** Keep dry offerings (*mishri*, *boondi*) in a dry hopper with an airtight top lid to prevent ambient humidity from softening sugar crystals.

---

## 10. Troubleshooting Guide

| Issue | Root Cause | Solution |
| :--- | :--- | :--- |
| **Motor doesn't spin on hand detection** | AO3400 Gate not connected or external PSU OFF | Verify external 5V/6V supply is powered; verify D5 connection to MOSFET gate with 100&Omega; resistor. Check common GND between ESP8266 and motor PSU. |
| **Motor turns during boot / flashing** | Floating MOSFET Gate | Verify 10k&Omega; pulldown resistor is installed between AO3400 Gate and GND. |
| **VL53L0X sensor not detected** | I2C wiring issue or sensor address lockup | Check SDA &rarr; D2 and SCL &rarr; D1. Ensure 3.3V is connected (not 5V). Firmware automatically attempts I2C recovery every 3 seconds. |
| **Motor stutters or ESP8266 resets on spin** | Inductive back-EMF spike or voltage sag | Ensure **flyback diode (1N5819 or 1N4007)** is installed across motor terminals. Ensure motor runs from external 5V/6V supply, NOT the ESP8266 3.3V regulator pin! |
| **Captive portal doesn't pop up** | OS captive portal suppression | Open your browser and navigate directly to `http://192.168.4.1` or `http://prasad.local`. |
| **Prasad portion is too large / small** | Motor run duration needs tuning | Connect to dashboard and adjust **Motor Run Duration** (e.g. increase from 1200ms to 1800ms for larger dose, or decrease for smaller dose). Click *SAVE SETTINGS TO EEPROM*. |
| **Devotees trigger double dispense** | Hand lingering too close to sensor | Increase **Removal Hysteresis** to 3.0 cm or increase **Cooldown Time** to 2000 ms in the Web Dashboard. |

---

## 👥 Authors & Open-Source Hardware License

Developed with devotion for clean, touchless, dignified prasad distribution in temples worldwide. Released under the open-source **MIT License** — free to build, modify, distribute, and enhance for temples, community mandirs, and cultural institutions everywhere.
