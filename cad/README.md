# Automatic Prasad Dispenser V3 — CAD & 3D Printing Manual

This directory contains all 3D mechanical CAD files, slicer project files, parametric generator scripts, and assembly specifications for the **Automatic Touchless Prasad Dispenser V3**.

---

## 1. Directory File Index

| File | Description | Dimensions (X × Y × Z) | Triangles | Print Plate |
| :--- | :--- | :--- | :--- | :--- |
| **`prasad_v3.1.3mf`** | Complete Bambu Studio / OrcaSlicer project containing all 7 build plates | Pre-oriented & configured | All Parts | Plates 1–7 |
| **`body top.stl`** | Top chassis housing, hopper receiver collar, upper gear enclosure | 103.6 × 81.4 × 117.8 mm | 16,322 | Plate 1 |
| **`body_bottom.stl`** | Main lower chassis, motor cradle, shaft bearing bosses, discharge port | 110.4 × 80.1 × 99.9 mm | 11,104 | Plate 2 |
| **`main gear.stl`** | 63mm high-torque spur gear (120T, 0.5M) with D-bore for main drive shaft | 63.2 × 20.6 × 63.0 mm | 9,134 | Plate 3 |
| **`motor_gear.stl`** | Motor pinion gear with 3mm D-shaft bore for N20 micro gearmotor | 15.3 × 10.4 × 15.0 mm | 1,800 | Plate 3 |
| **`main rotor.stl`** | Standard 4-blade dispenser rotor barrel with D-bore shaft mount | 49.2 × 52.3 × 51.4 mm | 1,320 | Plate 4 |
| **`4_blade.stl`** | Alternative 4-blade rotor (49.2mm height) for low-clearance hoppers | 49.2 × 52.3 × 49.2 mm | 1,320 | Plate 7 |
| **`nozzle.stl`** | Downward dispensing funnel with integrated VL53L0X ToF sensor bracket | 40.1 × 30.3 × 35.0 mm | 1,000 | Plate 5 |
| **`motor_cover.stl`** | N20 gearmotor clamping bracket & retention cover plate | 5.0 × 30.0 × 30.0 mm | 296 | Plate 6 |
| **`main_axis.stl`** | 101mm primary drive shaft with bearing shoulders & dual D-flats | 14.3 × 101.1 × 13.7 mm | 640 | Plate 6 |
| **`prasad_parametric_generator.scad`** | OpenSCAD parametric generator for custom blades, gears, nozzles & shafts | Parametric | Open-Source | Customizer |
| **`viewer.html`** | Standalone interactive 3D WebGL viewer to inspect models in your browser | HTML5 / WebGL | Three.js | Local Browser |

---

## 2. Print Settings & Slicer Profile

The pre-sliced **`prasad_v3.1.3mf`** is configured for **Bambu Lab A1** (compatible with X1C, P1S, A1 Mini, Prusa MK3/MK4, Ender 3, and Voron printers).

### Recommended General Print Parameters

| Parameter | Recommended Value | Notes |
| :--- | :--- | :--- |
| **Layer Height** | `0.20 mm` | 0.16 mm optional for gears (`motor_gear`, `main gear`) |
| **First Layer Height** | `0.20 mm` | Ensures solid bed adhesion without elephant's foot |
| **Wall Perimeters (Loops)** | `3 to 4 walls` | Critical: gears and shaft need high strength |
| **Top / Bottom Layers** | `4 Top / 4 Bottom` | Solid enclosure walls |
| **Infill Density** | `15% – 25%` | `15% Grid / Gyroid` for chassis; `30% Gyroid` for gears and shaft |
| **Infill Pattern** | `Gyroid` or `Grid` | Gyroid provides isotropic rigidity across all axes |
| **Support Type** | `Tree (auto)` | Only required for overhangs on `body top` and `body_bottom` |
| **Support Overhang Angle** | `55°` | Minimizes support contact while bridging bearing pockets |
| **Brim** | `Auto / Mouse-ears` | Recommended for tall narrow parts like `main_axis.stl` |

### Recommended Filament Materials

1. **Food-Contact Parts (`main rotor.stl`, `4_blade.stl`, `nozzle.stl`)**:
   - **Material:** **Food-Grade PETG** (e.g., FDA-compliant PETG) or **Certified PLA**.
   - **Color:** White, Natural Translucent, or Saffron / Gold.
   - *Why PETG?* High chemical resistance, smooth surface, odorless, easy to sanitize with warm soapy water, and temperature resistance up to 75°C.
   - *Flexible Blades Alternative:* `4_blade.stl` can be printed in **TPU 95A** for soft, ultra-gentle silicone-like paddle movement that never crushes fragile offerings like batasha or boondi.

2. **Gears & Drive Train (`motor_gear.stl`, `main gear.stl`, `main_axis.stl`)**:
   - **Material:** **PETG** or **Tough PLA** (Nylon PA-CF / PA-GF optional for industrial 24/7 temple use).
   - *Why?* Low friction coefficient, excellent shear strength on gear teeth, and wear resistance.

3. **Chassis & Enclosure (`body top.stl`, `body_bottom.stl`, `motor_cover.stl`)**:
   - **Material:** **PLA**, **PETG**, or **ABS/ASA**.
   - Rigid, dimensionally stable housing with mounting bosses for M3 hardware.

---

## 3. Bambu Studio Plate Layout (`prasad_v3.1.3mf`)

The included **`prasad_v3.1.3mf`** organizes the entire dispenser across 7 optimized build plates:

| Plate # | Parts Included | Primary Material | Estimated Print Time |
| :---: | :--- | :--- | :--- |
| **Plate 1** | `body top.stl` | PLA / PETG | ~3 hr 45 min |
| **Plate 2** | `body_bottom.stl` | PLA / PETG | ~3 hr 15 min |
| **Plate 3** | `main gear.stl` + `motor_gear.stl` | Tough PLA / PETG | ~1 hr 20 min |
| **Plate 4** | `main rotor.stl` (Standard 51.4mm) | Food-Grade PETG / TPU | ~55 min |
| **Plate 5** | `nozzle.stl` (Spout + Sensor Mount) | Food-Grade PETG | ~40 min |
| **Plate 6** | `motor_cover.stl` + `main_axis.stl` | Tough PLA / PETG | ~45 min |
| **Plate 7** | `4_blade.stl` (Compact 49.2mm variant) | Food-Grade PETG / TPU | ~50 min |

*(Extracted high-resolution build plate previews are available in [`cad/previews/`](file:///c:/Users/Excel/Desktop/Prasad/cad/previews/))*

---

## 4. Mechanical Design & Gear Ratio Mechanics

### Torque Multiplication & Gearbox Principles
Dispensing viscous or irregular dry foods (crystallized sugar mishri, roasted chickpeas, dry fruit, puffed rice) requires high instantaneous starting torque to overcome particle interlock:
- **Motor:** N20 DC Micro Gearmotor (6V DC, internal reduction ~100:1 to 298:1, ~60–100 RPM output).
- **Pinion Gear:** `motor_gear.stl` (Diameter = 15.27 mm, ~28 teeth, Module 0.5).
- **Spur Gear:** `main gear.stl` (Diameter = 63.15 mm, ~120 teeth, Module 0.5).
- **External Reduction Ratio:**
  $$\text{Gear Ratio} = \frac{63.15\text{ mm}}{15.27\text{ mm}} \approx 4.13 : 1$$
- **Torque Multiplication:** The gear train multiplies the N20 motor output torque by **over 4.1×**, delivering immense rotational force to the rotor shaft while reducing output speed to a smooth, controlled ~15–25 RPM.
- **Portion Metering:** With a 4-blade rotor, one single quadrant rotation ($90^\circ$) discharges exactly one pocket dose. At ~15 RPM, one $90^\circ$ advance takes **~1.0 to 1.8 seconds**, aligning with the default firmware motor run time of `1800 ms`.

```
       [N20 Gearmotor (60 RPM)]
                 |
         (3mm D-Shaft)
                 v
       [motor_gear.stl (15.27mm)]
                 |
            meshes with (4.13:1 reduction)
                 v
       [main gear.stl (63.15mm)]
                 |
         (8mm Main Shaft)
                 v
       [main rotor.stl (4-blade)] ---> Advances 1 dose per 90° turn
                 |
                 v
       [nozzle.stl Funnel] ----------> Drops directly into devotee's hands
```

---

## 5. Step-by-Step Mechanical Assembly Guide

### Required Fasteners & Mechanical Hardware

| Item | Specification | Qty | Purpose |
| :--- | :--- | :---: | :--- |
| **N20 Micro Gearmotor** | 6V DC, 60–100 RPM with 3mm D-shaft | 1 | Primary drive motor |
| **M3 Screws** | M3 × 12mm button head socket cap | 4 | Motor cover clamp to chassis |
| **M3 Screws** | M3 × 16mm button head socket cap | 4 | Joining `body top` to `body_bottom` |
| **M3 Screws** | M3 × 8mm socket cap | 2 | Mounting `nozzle.stl` to bottom outlet |
| **M2.5 or M3 Screws**| M2.5 × 6mm or M3 × 6mm with nuts | 2 | Fastening VL53L0X sensor to nozzle bracket |
| **M3 Threaded Inserts**| M3 × 4mm (OD 4.2mm) heat-set brass inserts | 10 | Heat-pressed into chassis boss holes |
| **Ball Bearings (Optional)**| MR128ZZ (8×12×3.5mm) or 608RS | 2 | Ultra-low friction main shaft rotation (or print bushings) |

---

### Assembly Sequence

```
                         [ PRASAD HOPPER / RESERVOIR ]
                                      |
                                      v
       +--------------------------------------------------------------+
       |                        BODY TOP.STL                          |
       |  - Hopper receiving flange                                   |
       |  - Upper rotor chamber half                                  |
       |  - Gear train dust shield                                    |
       +--------------------------------------------------------------+
                                      |
                     [ 4-BLADE ROTOR ] on [ MAIN AXIS SHAFT ]
                                      |  meshes with
                   [ MAIN GEAR ] <----+----> [ MOTOR PINION GEAR ]
                                                     ^
                                                     |
                                            [ N20 MOTOR + COVER ]
                                      |
       +--------------------------------------------------------------+
       |                       BODY_BOTTOM.STL                        |
       |  - Motor cradle & clamp bosses                               |
       |  - Lower rotor chamber & drop chute                          |
       |  - Internal electronics & MOSFET compartment                 |
       +--------------------------------------------------------------+
                                      |
                                      v
       +--------------------------------------------------------------+
       |                          NOZZLE.STL                          |
       |  - Dispensing spout funnel (food drop path)                  |
       |  - Downward VL53L0X Time-of-Flight sensor mount bracket     |
       +--------------------------------------------------------------+
                                      |
                                      v
                         [ DEVOTEE'S CUPPED HANDS ]
```

#### Step 1: Chassis Clean-Up & Heat-Set Inserts
1. Remove any temporary tree supports from `body_bottom.stl` and `body top.stl`.
2. Inspect the shaft bearing pockets (8mm diameter) to ensure smooth, round bores. If using printed bushings, lightly ream with an 8mm drill bit or rolled sandpaper.
3. Using a soldering iron set to 230°C–250°C, heat-set the 10 brass M3 threaded inserts into the mounting bosses:
   - 4 inserts on `body_bottom.stl` perimeter for joining `body top`.
   - 4 inserts around the motor cradle for `motor_cover.stl`.
   - 2 inserts at the bottom discharge port for `nozzle.stl`.

#### Step 2: N20 Motor & Pinion Gear Installation
1. Take the N20 micro gearmotor and slide `motor_gear.stl` onto its 3mm D-shaft until flush with the gearbox face.
   *(The gear has a matched D-flat inside the bore; if snug, gently press-fit with a vise or small clamp).*
2. Place the N20 motor into the rectangular motor bay on `body_bottom.stl`.
3. Place `motor_cover.stl` over the motor body and secure using 4× M3 × 12mm screws. Hand-tighten firmly so the motor cannot twist under torque.

#### Step 3: Main Drive Shaft (`main_axis`) & Main Gear
1. Insert `main_axis.stl` through the main chassis bearing boss.
2. Slide `main gear.stl` onto the shaft's D-flat section. Verify that `main gear.stl` aligns with and smoothly meshes with `motor_gear.stl`.
3. Spin the gears by hand — they should rotate freely with minimal backlash (~0.2mm gear clearance).

#### Step 4: Dispenser Chamber & Rotor Installation
1. Inside the dispensing barrel cavity of `body_bottom.stl`, slide `main rotor.stl` (or `4_blade.stl`) onto the drive end of `main_axis.stl`.
2. The rotor's internal D-bore locks onto the shaft, ensuring zero rotational slip.
3. Ensure the rotor spins symmetrically inside the cylinder with ~1mm clearance around the chamber wall.

#### Step 5: Dispensing Nozzle & VL53L0X Sensor Mount
1. Place the STMicroelectronics **VL53L0X sensor** board into the mounting bracket on `nozzle.stl` with its optical emitter/receiver lenses facing downwards toward where devotees cup their hands.
2. Fasten the sensor using 2× M2.5 or M3 screws and nuts.
3. Route the 4-pin I2C wire harness (VIN, GND, SDA, SCL) through the internal wiring conduit into the electronics bay.
4. Align `nozzle.stl` with the discharge chute at the bottom of `body_bottom.stl` and secure with 2× M3 × 8mm screws.

#### Step 6: Electronics Bay & Enclosure Closure
1. Solder the AO3400 MOSFET driver circuit and flyback diode onto a small protoboard or direct harness (see [`prasad/WIRING_AND_SETUP.md`](file:///c:/Users/Excel/Desktop/Prasad/prasad/WIRING_AND_SETUP.md)).
2. Connect the N20 motor leads across the MOSFET drain and flyback diode.
3. Connect the VL53L0X I2C lines to NodeMCU ESP8266 pins D2 (SDA) and D1 (SCL).
4. Connect the Status LED to D4.
5. Place the NodeMCU ESP8266 board into the internal chassis cradle.
6. Lower `body top.stl` onto `body_bottom.stl`, ensuring no internal wires are pinched.
7. Secure using 4× M3 × 16mm screws around the chassis perimeter.
8. Seat the clear prasad storage hopper / cylinder onto the top inlet neck.

---

## 6. Parametric Customization (OpenSCAD)

If you wish to modify the mechanism for different types of prasad or custom hardware:
1. Open **[`cad/prasad_parametric_generator.scad`](file:///c:/Users/Excel/Desktop/Prasad/cad/prasad_parametric_generator.scad)** in [OpenSCAD](https://openscad.org/).
2. Open the **Customizer** panel (`Window` &rarr; `Customizer`).
3. You can customize:
   - **Blade Count (`rotor_blade_count`):** Change from 4 blades to 3, 5, or 6 blades depending on desired dose volume.
   - **Rotor Diameter (`rotor_outer_diameter`):** Scale the chamber from 30mm up to 80mm.
   - **Nozzle Exit (`nozzle_exit_width`, `nozzle_exit_depth`):** Widen for large pedas/laddoos, or narrow for fine grains/mishri.
   - **Gear Teeth & Module:** Adjust gear ratio from 3:1 to 6:1.
   - **Shaft Tolerances:** Fine-tune shaft bore clearance for your 3D printer calibration.
4. Press `F6` to render, then `F7` to export as an STL file.

---

## 7. Interactive 3D Web Viewer

To inspect the 3D models interactively without needing CAD software:
- Open **[`cad/viewer.html`](file:///c:/Users/Excel/Desktop/Prasad/cad/viewer.html)** in any modern web browser (Google Chrome, Edge, Firefox, Safari).
- Features:
  - 3D orbit, pan, and zoom controls.
  - Interactive part selector to inspect each STL part individually.
  - Exploded view slider to see how all components align.
  - Wireframe and X-ray / transparent modes to inspect internal gear mesh.
  - Drag-and-drop STL loading: Drop any custom `.stl` file directly into the viewer window.

---

## 8. Food Safety & Sanitization Guidelines

When dispensing food offerings in temples and homes:
1. **Material Selection:** Use virgin, certified food-safe PETG or PLA filament without toxic heavy-metal pigments.
2. **Smooth Surfaces:** Print the food-contact parts (`rotor`, `nozzle`, inside of `body top`) with tight layer heights (`0.16mm`) to reduce layer ridges where food dust can accumulate.
3. **Optional Food-Safe Epoxy:** For high-volume temple environments, brush food-safe epoxy resin (e.g., FDA 21 CFR 175.300 compliant resin) over the internal dispensing chute for a glass-smooth, washable finish.
4. **Cleaning Routine:**
   - The nozzle and rotor can be removed by unscrewing 2 M3 screws.
   - Wash with lukewarm water and mild dish soap. Do not wash PLA in boiling water or dishwashers above 55°C.
