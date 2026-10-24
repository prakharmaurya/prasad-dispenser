/*
 * =================================================================================
 *  AUTOMATIC PRASAD DISPENSER - ESP8266 + VL53L0X + N20 DC MOTOR (AO3400 MOSFET)
 * =================================================================================
 *  Hardware Connections:
 *    - ESP8266 NodeMCU / D1 Mini:
 *        D5 (GPIO 14) -> AO3400 MOSFET Gate (100R series resistor, 10k pulldown to GND)
 *        D2 (GPIO 4)  -> VL53L0X SDA (I2C Data)
 *        D1 (GPIO 5)  -> VL53L0X SCL (I2C Clock)
 *        D4 (GPIO 2)  -> Onboard Status LED (Active-LOW)
 *        3.3V         -> VL53L0X VIN
 *        GND          -> Common GND (ESP8266, VL53L0X, MOSFET Source, Power Supply GND)
 *
 *    - N20 DC Motor & Driver Circuit:
 *        External 5V/6V Power (+) -> Motor (+) & Flyback Diode Cathode (stripe)
 *        Motor (-)                -> AO3400 Drain & Flyback Diode Anode
 *        AO3400 Source            -> Common GND
 *        AO3400 Gate              -> 100 ohm resistor -> ESP8266 D5 (GPIO 14)
 *        (10k pulldown resistor from Gate to GND to prevent motor spin on boot)
 *        (Flyback diode 1N5819 or 1N4007 across motor terminals is MANDATORY)
 *
 *  Wi-Fi Access Point Details:
 *    - SSID:     Prasad-Dispenser
 *    - Password: password123
 *    - Web UI:   http://192.168.4.1 or http://prasad.local
 *    - Captive Portal: Automatically launches dashboard on phone connection
 * =================================================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <EEPROM.h>

// ---------------------------------------------------------------------------------
// PIN DEFINITIONS
// ---------------------------------------------------------------------------------
#ifdef D5
const uint8_t PIN_MOTOR      = D5;  // GPIO 14: AO3400 MOSFET Gate (Digital HIGH/LOW)
const uint8_t PIN_I2C_SDA    = D2;  // GPIO 4:  VL53L0X SDA
const uint8_t PIN_I2C_SCL    = D1;  // GPIO 5:  VL53L0X SCL
const uint8_t PIN_STATUS_LED = D4;  // GPIO 2:  Onboard LED (Active LOW)
#else
const uint8_t PIN_MOTOR      = 14;
const uint8_t PIN_I2C_SDA    = 4;
const uint8_t PIN_I2C_SCL    = 5;
const uint8_t PIN_STATUS_LED = 2;
#endif

// ---------------------------------------------------------------------------------
// SYSTEM CONFIGURATION & EEPROM STORAGE
// ---------------------------------------------------------------------------------
#define EEPROM_MAGIC 0x50525306 // "PRS\6" - version identifier for config validity (added ledActiveHigh)

struct SystemConfig {
  uint32_t magic;              // Validation magic number
  float    triggerDistanceCm;  // Hand placement distance to trigger dispense (e.g. 16.0 cm)
  float    hysteresisCm;       // Distance delta hand must retreat to reset (e.g. 2.0 cm)
  uint16_t motorDurationMs;    // Run time of N20 motor (e.g. 1800 ms)
  uint16_t cooldownMs;         // Cooldown delay before next devotee can receive (e.g. 1500 ms)
  uint16_t preDispenseDelayMs; // Delay to settle hands before motor starts (e.g. 100 ms)
  uint8_t  triggerSamples;     // Consecutive samples required to trigger (debounce)
  uint8_t  ledActiveHigh;      // 1 = Active-HIGH (External LED to GND: HIGH=ON), 0 = Active-LOW
  uint32_t totalDispenses;     // Cumulative dispense counter
  char     apSsid[32];         // Hotspot SSID
  char     apPassword[32];     // Hotspot Password
};

SystemConfig config;

// Default configuration factory values
void setFactoryDefaults() {
  config.magic              = EEPROM_MAGIC;
  config.triggerDistanceCm  = 16.0;
  config.hysteresisCm       = 2.0;
  config.motorDurationMs    = 1800;
  config.cooldownMs         = 1500;
  config.preDispenseDelayMs = 100;
  config.triggerSamples     = 3;
  config.ledActiveHigh      = 1; // 1 = Active-HIGH for external LED on D4 to GND
  config.totalDispenses     = 0;
  strncpy(config.apSsid, "Prasad-Dispenser", sizeof(config.apSsid));
  strncpy(config.apPassword, "password123", sizeof(config.apPassword));
}

void loadConfigFromEEPROM() {
  EEPROM.begin(sizeof(SystemConfig));
  SystemConfig stored;
  EEPROM.get(0, stored);
  if (stored.magic == EEPROM_MAGIC) {
    config = stored;
    Serial.println("[EEPROM] Configuration loaded successfully.");
  } else {
    Serial.println("[EEPROM] Initializing factory defaults...");
    setFactoryDefaults();
    EEPROM.put(0, config);
    EEPROM.commit();
  }
}

void saveConfigToEEPROM() {
  config.magic = EEPROM_MAGIC;
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("[EEPROM] Configuration saved to flash memory.");
}

// ---------------------------------------------------------------------------------
// SENSOR & STATE MACHINE DEFINITIONS
// ---------------------------------------------------------------------------------
VL53L0X sensor;
bool sensorAvailable = false;
unsigned long lastSensorRetryTime = 0;

enum DispenserState {
  STATE_IDLE,              // Waiting for devotee's hand
  STATE_SETTLE,            // Hand detected, settling delay before motor spins
  STATE_DISPENSING,        // Motor is actively running for configured duration
  STATE_WAIT_FOR_REMOVAL,  // Motor stopped. Waiting for devotee to remove hand
  STATE_COOLDOWN           // Hand removed. Cooldown timer running down before re-arming
};

DispenserState currentState = STATE_IDLE;

// Timing & measurement variables
unsigned long stateStartTime = 0;
unsigned long cooldownStartTime = 0;
unsigned long settleStartTime = 0;
unsigned long lastDispenseSaveTime = 0;
bool counterNeedsSave = false;
uint8_t sensorTimeoutCounter = 0;
float currentDistanceCm = 999.0;
uint16_t currentDistanceMm = 9990;
int consecutiveTriggerCount = 0;
int consecutiveRemovalCount = 0;
bool isManualTest = false;
bool dispenseAbortedEarly = false;

const int SAMPLES_BUFFER_SIZE = 4;
float distanceBuffer[SAMPLES_BUFFER_SIZE];
int bufferIndex = 0;
bool bufferFilled = false;
unsigned long lastSensorPollTime = 0;
const unsigned long SENSOR_POLL_INTERVAL_MS = 35; // Non-blocking sensor read interval

// ---------------------------------------------------------------------------------
// NETWORK & SERVER DEFINITIONS
// ---------------------------------------------------------------------------------
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

DNSServer dnsServer;
ESP8266WebServer server(80);

// ---------------------------------------------------------------------------------
// HARDWARE DRIVER FUNCTIONS (PURE DIGITAL FULL TORQUE ON/OFF)
// ---------------------------------------------------------------------------------
void startMotor() {
  digitalWrite(PIN_MOTOR, HIGH); // Full 100% drive voltage to AO3400 Gate
}

void stopMotor() {
  digitalWrite(PIN_MOTOR, LOW);  // Turn off AO3400 Gate immediately
}

bool isMotorRunning() {
  return (digitalRead(PIN_MOTOR) == HIGH);
}

void setStatusLed(bool on) {
  // If ledActiveHigh is 1 (External LED on D4 connected to GND): HIGH is ON
  // If ledActiveHigh is 0 (Active-LOW onboard module LED): LOW is ON
  if (config.ledActiveHigh) {
    digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
  } else {
    digitalWrite(PIN_STATUS_LED, on ? LOW : HIGH);
  }
}

bool isStatusLedOn() {
  if (config.ledActiveHigh) {
    return (digitalRead(PIN_STATUS_LED) == HIGH);
  } else {
    return (digitalRead(PIN_STATUS_LED) == LOW);
  }
}

// Dynamic visual LED signaling on ESP8266 onboard LED (Active-LOW)
// Pattern rules:
// - On Ready: Full steady glow (Solid ON - machine ready for devotee)
// - While Dispensing: Fast energetic strobe blink (80ms ON / 80ms OFF)
// - While Waiting: Distinct waiting blink patterns (200ms on removal, 400ms on cooldown)
// - On Ready again: Full steady glow
void updateLedPattern() {
  unsigned long now = millis();
  switch (currentState) {
    case STATE_IDLE:
      // Machine READY: Full steady glow (Solid ON)
      setStatusLed(true);
      break;
    case STATE_SETTLE:
      // Settle delay: Fast pulse (60ms) signals hand recognized & settling
      setStatusLed((now / 60) % 2 == 0);
      break;
    case STATE_DISPENSING:
      // DISPENSING: Fast energetic strobe blink (80ms ON / 80ms OFF)
      // Clearly indicates motor actively running & dispensing prasad
      setStatusLed((now / 80) % 2 == 0);
      break;
    case STATE_WAIT_FOR_REMOVAL:
      // WAITING FOR HAND REMOVAL: Distinct alert blink (200ms ON / 200ms OFF)
      // Signals devotee that dispense is done and prompts them to remove hand
      setStatusLed((now / 200) % 2 == 0);
      break;
    case STATE_COOLDOWN:
      // WAITING DURING COOLDOWN: Gentle slow pulse (400ms ON / 400ms OFF)
      // Signals cooldown pause is running before machine re-arms to ready
      setStatusLed((now / 400) % 2 == 0);
      break;
  }
}

// Initialize VL53L0X sensor safely with non-blocking timeouts
bool initializeSensor() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000); // 400kHz Fast Mode I2C

  if (!sensor.init()) {
    Serial.println("[Sensor] VL53L0X not detected! Check wiring.");
    return false;
  }

  // 50ms timeout prevents hanging on I2C communication
  sensor.setTimeout(50);
  sensor.setMeasurementTimingBudget(33000);
  sensor.startContinuous(33);
  Serial.println("[Sensor] VL53L0X initialized in continuous mode.");
  return true;
}

// Non-blocking distance reading with moving average filter.
// Returns true ONLY when a new physical measurement has been completed.
bool pollDistanceSensor() {
  unsigned long now = millis();
  if (now - lastSensorPollTime < SENSOR_POLL_INTERVAL_MS) {
    return false;
  }
  lastSensorPollTime = now;

  if (!sensorAvailable) {
    // Attempt non-blocking reconnection every 2.5 seconds
    if (now - lastSensorRetryTime > 2500) {
      lastSensorRetryTime = now;
      sensorAvailable = initializeSensor();
    }
    return false;
  }

  uint16_t range = sensor.readRangeContinuousMillimeters();
  if (sensor.timeoutOccurred()) {
    sensorTimeoutCounter++;
    currentDistanceMm = 8190;
    currentDistanceCm = 819.0;
    for (int i = 0; i < SAMPLES_BUFFER_SIZE; i++) {
      distanceBuffer[i] = 100.0;
    }
    bufferIndex = 0;
    bufferFilled = true;
    // Auto-recover I2C bus if sensor hangs consecutively (e.g. from inductive motor noise)
    if (sensorTimeoutCounter >= 5) {
      Serial.println("[Sensor] Consecutive I2C timeouts! Auto-recovering sensor bus...");
      sensorAvailable = initializeSensor();
      sensorTimeoutCounter = 0;
    }
    return true; // Timeout event represents valid out-of-range sample
  }
  sensorTimeoutCounter = 0;

  // VL53L0X returns 8190 or 8191 when target is out of detection range
  if (range == 0 || range >= 8000) {
    currentDistanceMm = 8190;
    currentDistanceCm = 819.0;
    // Keep moving average buffer in sync with clear/empty space
    for (int i = 0; i < SAMPLES_BUFFER_SIZE; i++) {
      distanceBuffer[i] = 100.0;
    }
    bufferIndex = 0;
    bufferFilled = true;
  } else {
    currentDistanceMm = range;
    float rawCm = (float)range / 10.0;

    // Moving average filter
    distanceBuffer[bufferIndex] = rawCm;
    bufferIndex = (bufferIndex + 1) % SAMPLES_BUFFER_SIZE;
    if (bufferIndex == 0) bufferFilled = true;

    int count = bufferFilled ? SAMPLES_BUFFER_SIZE : bufferIndex;
    if (count == 0) count = 1;

    float sum = 0;
    for (int i = 0; i < count; i++) {
      sum += distanceBuffer[i];
    }
    currentDistanceCm = sum / (float)count;
  }

  return true; // Fresh physical sensor sample ready
}

// Start dispensing cycle
void triggerDispense() {
  if (currentState == STATE_DISPENSING) return; // Already dispensing

  Serial.println("[Dispenser] Hand verified -> Running N20 motor!");
  currentState = STATE_DISPENSING;
  stateStartTime = millis();
  dispenseAbortedEarly = false;
  consecutiveRemovalCount = 0;
  consecutiveTriggerCount = 0;

  startMotor();      // Full motor power
  // Note: LED pattern is updated dynamically by updateLedPattern()

  config.totalDispenses++;
  counterNeedsSave = true;
  lastDispenseSaveTime = millis();
}

// Emergency stop
void emergencyStop() {
  stopMotor();
  isManualTest = false;
  dispenseAbortedEarly = false;
  currentState = STATE_IDLE;
  setStatusLed(true); // Full steady glow on ready
  consecutiveTriggerCount = 0;
  consecutiveRemovalCount = 0;
  cooldownStartTime = 0;
  settleStartTime = 0;
  Serial.println("[Dispenser] EMERGENCY MOTOR CUTOFF executed!");
}

// ---------------------------------------------------------------------------------
// WEB UI (MODERN, RESPONSIVE, REAL-TIME TELEMETRY IN PROGMEM)
// ---------------------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Prasad Dispenser Controller</title>
  <style>
    :root {
      --bg: #090d16;
      --card-bg: rgba(20, 27, 45, 0.90);
      --card-border: rgba(245, 158, 11, 0.22);
      --primary: #f59e0b;
      --primary-hover: #d97706;
      --primary-glow: rgba(245, 158, 11, 0.35);
      --accent: #10b981;
      --danger: #ef4444;
      --text: #f8fafc;
      --text-muted: #94a3b8;
      --input-bg: rgba(15, 23, 42, 0.85);
      --radius: 14px;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body {
      background: radial-gradient(circle at 50% 0%, #1e1b4b 0%, var(--bg) 65%);
      color: var(--text);
      min-height: 100vh;
      padding: 16px;
      display: flex;
      flex-direction: column;
      align-items: center;
    }
    .container {
      width: 100%;
      max-width: 480px;
      display: flex;
      flex-direction: column;
      gap: 14px;
    }
    header {
      text-align: center;
      padding: 8px 0 2px;
    }
    .logo-badge {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      background: rgba(245, 158, 11, 0.12);
      border: 1px solid var(--primary-glow);
      padding: 6px 14px;
      border-radius: 30px;
      margin-bottom: 6px;
    }
    .logo-badge svg { width: 18px; height: 18px; fill: var(--primary); }
    .logo-badge span { color: var(--primary); font-size: 13px; font-weight: 700; letter-spacing: 0.5px; }
    h1 { font-size: 22px; font-weight: 800; color: #fff; letter-spacing: -0.5px; }
    .subhead { font-size: 12px; color: var(--text-muted); margin-top: 2px; }

    /* Status Banner */
    .status-banner {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 14px 18px;
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: var(--radius);
      backdrop-filter: blur(12px);
      box-shadow: 0 4px 20px rgba(0,0,0,0.3);
    }
    .status-indicator {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .status-dot {
      width: 14px;
      height: 14px;
      border-radius: 50%;
      background: var(--text-muted);
      transition: all 0.3s;
    }
    .status-dot.idle { background: #10b981; box-shadow: 0 0 12px #10b981; }
    .status-dot.dispensing { background: #f59e0b; box-shadow: 0 0 16px #f59e0b; animation: pulse 0.7s infinite; }
    .status-dot.waiting { background: #38bdf8; box-shadow: 0 0 14px #38bdf8; animation: pulse 1.2s infinite; }
    .status-dot.offline { background: #ef4444; box-shadow: 0 0 14px #ef4444; }
    @keyframes pulse {
      0%, 100% { transform: scale(1); opacity: 1; }
      50% { transform: scale(1.35); opacity: 0.6; }
    }
    .status-title { font-size: 14px; font-weight: 700; }
    .status-sub { font-size: 11px; color: var(--text-muted); }

    /* Real-Time Hardware Feedback Grid */
    .hw-feedback-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }
    .hw-badge {
      background: rgba(15, 23, 42, 0.75);
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 10px;
      padding: 10px 14px;
      display: flex;
      align-items: center;
      gap: 10px;
      transition: all 0.2s;
    }
    .hw-badge.active-motor {
      border-color: #f59e0b;
      background: rgba(245, 158, 11, 0.12);
      box-shadow: 0 0 12px rgba(245, 158, 11, 0.25);
    }
    .hw-badge.active-led {
      border-color: #10b981;
      background: rgba(16, 185, 129, 0.12);
      box-shadow: 0 0 12px rgba(16, 185, 129, 0.25);
    }
    .hw-badge-icon {
      width: 28px;
      height: 28px;
      border-radius: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
      background: rgba(255, 255, 255, 0.08);
    }
    .hw-badge-icon svg { width: 16px; height: 16px; fill: var(--text-muted); }
    .hw-badge.active-motor .hw-badge-icon svg { fill: #f59e0b; animation: spin 1s linear infinite; }
    .hw-badge.active-led .hw-badge-icon svg { fill: #10b981; }
    @keyframes spin { 100% { transform: rotate(360deg); } }
    .hw-badge-label { font-size: 11px; color: var(--text-muted); font-weight: 600; text-transform: uppercase; }
    .hw-badge-state { font-size: 13px; font-weight: 800; color: #fff; }

    /* Live Distance Radar Card */
    .radar-card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: var(--radius);
      padding: 16px;
      backdrop-filter: blur(10px);
      box-shadow: 0 8px 24px rgba(0, 0, 0, 0.35);
    }
    .distance-display {
      display: flex;
      justify-content: space-between;
      align-items: baseline;
      margin-bottom: 8px;
    }
    .dist-num {
      font-size: 38px;
      font-weight: 800;
      color: var(--primary);
      letter-spacing: -1px;
      line-height: 1;
    }
    .dist-unit { font-size: 18px; color: var(--text-muted); font-weight: 600; margin-left: 4px; }
    .dist-label { font-size: 11px; font-weight: 600; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.5px; }

    /* Progress visual bar with BOTH threshold indicators */
    .dist-track {
      position: relative;
      width: 100%;
      height: 20px;
      background: rgba(15, 23, 42, 0.95);
      border-radius: 10px;
      overflow: hidden;
      border: 1px solid rgba(255, 255, 255, 0.08);
      margin: 10px 0;
    }
    .dist-fill {
      height: 100%;
      width: 0%;
      background: linear-gradient(90deg, #10b981 0%, #f59e0b 75%, #ef4444 100%);
      border-radius: 10px;
      transition: width 0.12s ease-out;
    }
    .dist-line-trigger {
      position: absolute;
      top: 0;
      bottom: 0;
      width: 3px;
      background: #f87171;
      box-shadow: 0 0 6px #f87171;
      z-index: 2;
    }
    .dist-line-reset {
      position: absolute;
      top: 0;
      bottom: 0;
      width: 3px;
      background: #38bdf8;
      box-shadow: 0 0 6px #38bdf8;
      z-index: 2;
    }
    .thresholds-legend {
      display: flex;
      justify-content: space-between;
      font-size: 11px;
      font-weight: 700;
      padding: 0 2px;
    }
    .legend-trigger { color: #f87171; }
    .legend-reset { color: #38bdf8; }

    /* Cooldown Countdown Banner */
    .cooldown-banner {
      display: none;
      align-items: center;
      justify-content: space-between;
      background: rgba(56, 189, 248, 0.12);
      border: 1px solid rgba(56, 189, 248, 0.3);
      padding: 8px 14px;
      border-radius: 8px;
      margin-top: 10px;
      font-size: 12px;
      font-weight: 700;
      color: #38bdf8;
    }

    /* Cards */
    .card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: var(--radius);
      padding: 16px;
      backdrop-filter: blur(10px);
      box-shadow: 0 8px 24px rgba(0, 0, 0, 0.35);
    }
    .card-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 14px;
      font-size: 14px;
      font-weight: 700;
      color: #f1f5f9;
      border-bottom: 1px solid rgba(255, 255, 255, 0.06);
      padding-bottom: 8px;
    }
    .card-header span { color: var(--primary); font-size: 12px; }

    /* Form Fields */
    .form-group {
      margin-bottom: 14px;
    }
    .form-label {
      display: flex;
      justify-content: space-between;
      font-size: 12px;
      font-weight: 600;
      color: var(--text-muted);
      margin-bottom: 6px;
    }
    .val-tag {
      color: var(--primary);
      font-weight: 700;
    }
    .input-row {
      display: flex;
      gap: 8px;
      align-items: center;
    }
    input[type="number"] {
      flex: 1;
      background: var(--input-bg);
      border: 1px solid rgba(255, 255, 255, 0.12);
      border-radius: 8px;
      padding: 8px 12px;
      color: #fff;
      font-size: 15px;
      font-weight: 600;
      text-align: center;
      outline: none;
    }
    input[type="number"]:focus {
      border-color: var(--primary);
      box-shadow: 0 0 0 2px var(--primary-glow);
    }
    .stepper-btn {
      width: 36px;
      height: 36px;
      border-radius: 8px;
      background: rgba(255, 255, 255, 0.08);
      border: 1px solid rgba(255, 255, 255, 0.12);
      color: #fff;
      font-size: 18px;
      font-weight: 700;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      transition: all 0.2s;
    }
    .stepper-btn:hover { background: rgba(255, 255, 255, 0.16); }
    .stepper-btn:active { transform: scale(0.92); }

    /* Quick Chips */
    .chips-row {
      display: flex;
      gap: 6px;
      margin-top: 6px;
      overflow-x: auto;
    }
    .chip {
      background: rgba(255, 255, 255, 0.06);
      border: 1px solid rgba(255, 255, 255, 0.1);
      padding: 4px 10px;
      border-radius: 14px;
      font-size: 11px;
      color: var(--text-muted);
      cursor: pointer;
      font-weight: 600;
      white-space: nowrap;
    }
    .chip:hover { border-color: var(--primary); color: #fff; }

    /* Action Buttons */
    .btn-trigger {
      width: 100%;
      background: linear-gradient(135deg, #f59e0b, #d97706);
      color: #000;
      border: none;
      padding: 13px 20px;
      border-radius: 10px;
      font-size: 15px;
      font-weight: 800;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 10px;
      box-shadow: 0 4px 16px var(--primary-glow);
      transition: all 0.2s;
    }
    .btn-trigger:hover { transform: translateY(-1px); box-shadow: 0 6px 20px rgba(245, 158, 11, 0.5); }
    .btn-trigger:active { transform: translateY(1px); }

    .btn-stop {
      width: 100%;
      background: rgba(239, 68, 68, 0.15);
      border: 1px solid rgba(239, 68, 68, 0.4);
      color: #f87171;
      padding: 10px 16px;
      border-radius: 10px;
      font-size: 13px;
      font-weight: 700;
      cursor: pointer;
      transition: all 0.2s;
      margin-top: 8px;
    }
    .btn-stop:hover { background: rgba(239, 68, 68, 0.25); color: #fff; }

    .btn-save {
      width: 100%;
      background: linear-gradient(135deg, #10b981, #059669);
      color: #fff;
      border: none;
      padding: 13px 20px;
      border-radius: 10px;
      font-size: 14px;
      font-weight: 800;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      box-shadow: 0 4px 16px rgba(16, 185, 129, 0.3);
      transition: all 0.2s;
    }
    .btn-save:hover { transform: translateY(-1px); box-shadow: 0 6px 20px rgba(16, 185, 129, 0.45); }

    .btn-reset {
      background: transparent;
      border: none;
      color: var(--text-muted);
      font-size: 12px;
      text-decoration: underline;
      cursor: pointer;
      text-align: center;
      width: 100%;
      padding: 6px;
    }
    .btn-reset:hover { color: #f87171; }

    /* Counter card */
    .stats-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }
    .stat-box {
      background: rgba(15, 23, 42, 0.7);
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 10px;
      padding: 10px 12px;
      text-align: center;
    }
    .stat-val { font-size: 20px; font-weight: 800; color: #fff; }
    .stat-lbl { font-size: 11px; color: var(--text-muted); font-weight: 600; margin-top: 2px; }

    /* Toast Notification */
    .toast {
      position: fixed;
      bottom: 20px;
      left: 50%;
      transform: translateX(-50%) translateY(100px);
      background: #10b981;
      color: #fff;
      padding: 10px 20px;
      border-radius: 30px;
      font-size: 13px;
      font-weight: 700;
      box-shadow: 0 6px 20px rgba(0,0,0,0.5);
      transition: transform 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
      z-index: 999;
      pointer-events: none;
    }
    .toast.show { transform: translateX(-50%) translateY(0); }

    /* Settings Guide Card Styles */
    .guide-item {
      padding: 10px 0;
      border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    }
    .guide-item:last-child { border-bottom: none; }
    .guide-title {
      font-size: 13px;
      font-weight: 700;
      color: #fff;
      display: flex;
      align-items: center;
      gap: 6px;
      margin-bottom: 4px;
    }
    .guide-badge {
      font-size: 10px;
      padding: 2px 7px;
      border-radius: 10px;
      background: rgba(245, 158, 11, 0.15);
      color: var(--primary);
      font-weight: 700;
    }
    .guide-desc {
      font-size: 12px;
      color: var(--text-muted);
      line-height: 1.45;
    }
    .guide-toggle {
      background: none;
      border: none;
      color: var(--primary);
      font-size: 12px;
      font-weight: 700;
      cursor: pointer;
      display: flex;
      align-items: center;
      gap: 4px;
    }
  </style>
</head>
<body>
  <div class="container">
    <!-- Header -->
    <header>
      <div class="logo-badge">
        <svg viewBox="0 0 24 24"><path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5"/></svg>
        <span>PRASAD AUTOMATION</span>
      </div>
      <h1>Prasad Dispenser V3</h1>
      <div class="subhead">ESP8266 + VL53L0X ToF Sensor + N20 DC Motor</div>
    </header>

    <!-- Real-time Status Banner -->
    <div class="status-banner">
      <div class="status-indicator">
        <div class="status-dot idle" id="statusDot"></div>
        <div>
          <div class="status-title" id="statusTitle">READY (IDLE)</div>
          <div class="status-sub" id="statusSub">Waiting for devotee's hand</div>
        </div>
      </div>
      <div style="text-align: right;">
        <span style="font-size: 11px; color: var(--text-muted);">Total Served</span>
        <div style="font-size: 18px; font-weight: 800; color: var(--primary);" id="totalDispensesTop">0</div>
      </div>
    </div>

    <!-- Real-time Hardware Telemetry (Motor & LED states) -->
    <div class="hw-feedback-grid">
      <div class="hw-badge" id="hwMotorBadge">
        <div class="hw-badge-icon">
          <svg viewBox="0 0 24 24"><path d="M12 15.5A3.5 3.5 0 0 1 8.5 12 3.5 3.5 0 0 1 12 8.5a3.5 3.5 0 0 1 3.5 3.5 3.5 3.5 0 0 1-3.5 3.5m7.43-2.53c.04-.32.07-.64.07-.97 0-.33-.03-.66-.07-1l2.11-1.63c.19-.15.24-.42.12-.64l-2-3.46c-.12-.22-.39-.31-.61-.22l-2.49 1c-.52-.39-1.06-.73-1.69-.98l-.37-2.65A.506.506 0 0 0 14 2h-4c-.25 0-.46.18-.5.42l-.37 2.65c-.63.25-1.17.59-1.69.98l-2.49-1c-.22-.09-.49 0-.61.22l-2 3.46c-.13.22-.07.49.12.64L4.57 11c-.04.34-.07.67-.07 1 0 .33.03.65.07.97l-2.11 1.66c-.19.15-.25.42-.12.64l2 3.46c.12.22.39.3.61.22l2.49-1.01c.52.4 1.06.74 1.69.99l.37 2.65c.04.24.25.42.5.42h4c.25 0 .46-.18.5-.42l.37-2.65c.63-.26 1.17-.59 1.69-.99l2.49 1.01c.22.08.49 0 .61-.22l2-3.46c.12-.22.07-.49-.12-.64l-2.11-1.66Z"/></svg>
        </div>
        <div>
          <div class="hw-badge-label">N20 Motor</div>
          <div class="hw-badge-state" id="hwMotorState">STOPPED</div>
        </div>
      </div>

      <div class="hw-badge active-led" id="hwLedBadge">
        <div class="hw-badge-icon">
          <svg viewBox="0 0 24 24"><path d="M12 2A7 7 0 0 0 5 9c0 2.38 1.19 4.47 3 5.74V17a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1v-2.26c1.81-1.27 3-3.36 3-5.74a7 7 0 0 0-7-7M9 21a1 1 0 0 0 1 1h4a1 1 0 0 0 1-1v-1H9v1Z"/></svg>
        </div>
        <div>
          <div class="hw-badge-label">Status LED</div>
          <div class="hw-badge-state" id="hwLedState">READY (ON)</div>
        </div>
      </div>
    </div>

    <!-- Live Distance Radar Card -->
    <div class="radar-card">
      <div class="distance-display">
        <div>
          <div class="dist-label">Live Hand Distance</div>
          <div>
            <span class="dist-num" id="liveDistNum">--.-</span>
            <span class="dist-unit">cm</span>
          </div>
        </div>
        <div style="text-align: right;">
          <div class="dist-label">Trigger & Reset Points</div>
          <div style="font-size: 15px; font-weight: 700; color: #f87171;" id="radarTriggerVal">Trigger &lt; 16.0 cm</div>
          <div style="font-size: 13px; font-weight: 700; color: #38bdf8;" id="radarResetVal">Reset &gt; 18.0 cm</div>
        </div>
      </div>

      <!-- Distance Track Progress Bar with DUAL Lines -->
      <div class="dist-track">
        <div class="dist-fill" id="distFill"></div>
        <div class="dist-line-trigger" id="thresholdTriggerLine" style="left: 53.3%;"></div>
        <div class="dist-line-reset" id="thresholdResetLine" style="left: 60.0%;"></div>
      </div>
      <div class="thresholds-legend">
        <span class="legend-trigger" id="legendTriggerText">🔴 Trigger: &lt; 16.0 cm</span>
        <span class="legend-reset" id="legendResetText">🔵 Reset: &gt; 18.0 cm</span>
      </div>

      <!-- Live Cooldown Countdown banner -->
      <div class="cooldown-banner" id="cooldownBanner">
        <span id="cooldownBannerLabel">⏱️ Devotee Cooldown Active:</span>
        <span id="cooldownTimerVal">1000 ms</span>
      </div>
    </div>

    <!-- Live Controls: Trigger & Emergency Stop -->
    <button class="btn-trigger" id="btnTestDispense" onclick="triggerManualDispense()">
      <svg width="20" height="20" viewBox="0 0 24 24" fill="currentColor"><path d="M8 5v14l11-7z"/></svg>
      <span>TEST DISPENSE NOW (1 DOSE)</span>
    </button>
    <button class="btn-stop" onclick="emergencyStopMotor()">EMERGENCY MOTOR CUTOFF</button>

    <!-- Calibration & Timing Settings Card -->
    <div class="card">
      <div class="card-header">
        <div>Dispenser Parameters</div>
        <span>[Saved to EEPROM]</span>
      </div>

      <!-- Motor Run Duration -->
      <div class="form-group">
        <div class="form-label">
          <span>Motor Run Duration (Portion Dispense Time)</span>
          <span class="val-tag" id="valDuration">1800 ms</span>
        </div>
        <div class="input-row">
          <button class="stepper-btn" onclick="adjustInput('inputDuration', -50)">-</button>
          <input type="number" id="inputDuration" value="1800" min="100" max="10000" step="50" oninput="updateUIValues()">
          <button class="stepper-btn" onclick="adjustInput('inputDuration', 50)">+</button>
        </div>
        <!-- Quick Preset Chips -->
        <div class="chips-row">
          <div class="chip" onclick="setDuration(800)">800ms</div>
          <div class="chip" onclick="setDuration(1000)">1.0s</div>
          <div class="chip" onclick="setDuration(1200)">1.2s</div>
          <div class="chip" onclick="setDuration(1500)">1.5s</div>
          <div class="chip" onclick="setDuration(1800)">1.8s (Default)</div>
          <div class="chip" onclick="setDuration(2000)">2.0s</div>
        </div>
        <div style="font-size: 11px; color: var(--text-muted); margin-top: 4px;">Calibrate this run-time so exactly one blade pocket passes per dispense.</div>
      </div>

      <!-- Hand Settle Delay (Wait Before Dispense) -->
      <div class="form-group">
        <div class="form-label">
          <span>Hand Settle Delay (Wait Before Motor Starts)</span>
          <span class="val-tag" id="valPreDelay">100 ms</span>
        </div>
        <div class="input-row">
          <button class="stepper-btn" onclick="adjustInput('inputPreDelay', -25)">-</button>
          <input type="number" id="inputPreDelay" value="100" min="0" max="3000" step="25" oninput="updateUIValues()">
          <button class="stepper-btn" onclick="adjustInput('inputPreDelay', 25)">+</button>
        </div>
        <!-- Quick Settle Chips -->
        <div class="chips-row">
          <div class="chip" onclick="setPreDelay(0)">0ms (Instant)</div>
          <div class="chip" onclick="setPreDelay(50)">50ms</div>
          <div class="chip" onclick="setPreDelay(100)">100ms (Default)</div>
          <div class="chip" onclick="setPreDelay(150)">150ms</div>
          <div class="chip" onclick="setPreDelay(200)">200ms</div>
          <div class="chip" onclick="setPreDelay(300)">300ms</div>
        </div>
        <div style="font-size: 11px; color: var(--text-muted); margin-top: 4px;">Pause after hand detection to give devotee time to settle hands inside machine before motor turns on.</div>
      </div>

      <!-- Trigger Distance (Hand Placement) -->
      <div class="form-group">
        <div class="form-label">
          <span>Trigger Distance (Hand Placement)</span>
          <span class="val-tag" id="valTriggerDist">16.0 cm</span>
        </div>
        <div class="input-row">
          <button class="stepper-btn" onclick="adjustInput('inputTriggerDist', -0.5)">-</button>
          <input type="number" id="inputTriggerDist" value="16.0" min="2.0" max="40.0" step="0.5" oninput="updateUIValues()">
          <button class="stepper-btn" onclick="adjustInput('inputTriggerDist', 0.5)">+</button>
        </div>
        <div class="chips-row">
          <div class="chip" onclick="setTriggerDist(10.0)">10.0 cm</div>
          <div class="chip" onclick="setTriggerDist(12.0)">12.0 cm</div>
          <div class="chip" onclick="setTriggerDist(14.0)">14.0 cm</div>
          <div class="chip" onclick="setTriggerDist(16.0)">16.0 cm (Default)</div>
          <div class="chip" onclick="setTriggerDist(18.0)">18.0 cm</div>
        </div>
        <div style="font-size: 11px; color: var(--text-muted); margin-top: 4px;">Motor triggers when hand is placed closer than this distance.</div>
      </div>

      <!-- Hysteresis (Both distances clearly calculated) -->
      <div class="form-group">
        <div class="form-label">
          <span>Removal Hysteresis Gap</span>
          <span class="val-tag" id="valHysteresis">+2.0 cm (Reset at 18.0 cm)</span>
        </div>
        <div class="input-row">
          <button class="stepper-btn" onclick="adjustInput('inputHysteresis', -0.2)">-</button>
          <input type="number" id="inputHysteresis" value="2.0" min="0.5" max="15.0" step="0.1" oninput="updateUIValues()">
          <button class="stepper-btn" onclick="adjustInput('inputHysteresis', 0.2)">+</button>
        </div>
        <div style="font-size: 11px; color: var(--text-muted); margin-top: 4px;">Devotee must pull hand back beyond <b>Trigger + Hysteresis</b> to allow the next dispense.</div>
      </div>

      <!-- Cooldown Delay (Anti-Abuse Delay between Devotees) -->
      <div class="form-group">
        <div class="form-label">
          <span>Cooldown Time (Wait Before Next Devotee)</span>
          <span class="val-tag" id="valCooldown">1500 ms</span>
        </div>
        <div class="input-row">
          <button class="stepper-btn" onclick="adjustInput('inputCooldown', -100)">-</button>
          <input type="number" id="inputCooldown" value="1500" min="200" max="5000" step="100" oninput="updateUIValues()">
          <button class="stepper-btn" onclick="adjustInput('inputCooldown', 100)">+</button>
        </div>
        <!-- Quick Cooldown Chips -->
        <div class="chips-row">
          <div class="chip" onclick="setCooldown(500)">0.5s</div>
          <div class="chip" onclick="setCooldown(1000)">1.0s</div>
          <div class="chip" onclick="setCooldown(1500)">1.5s (Default)</div>
          <div class="chip" onclick="setCooldown(2000)">2.0s</div>
          <div class="chip" onclick="setCooldown(3000)">3.0s</div>
        </div>
        <div style="font-size: 11px; color: var(--text-muted); margin-top: 4px;">Minimum waiting pause between consecutive devotees.</div>
      </div>

      <!-- Trigger Debounce Samples -->
      <div class="form-group">
        <div class="form-label">
          <span>Sensor Debounce Filter</span>
          <span class="val-tag" id="valSamples">3 Samples</span>
        </div>
        <div class="input-row">
          <button class="stepper-btn" onclick="adjustInput('inputSamples', -1)">-</button>
          <input type="number" id="inputSamples" value="3" min="1" max="10" step="1" oninput="updateUIValues()">
          <button class="stepper-btn" onclick="adjustInput('inputSamples', 1)">+</button>
        </div>
        <div style="font-size: 11px; color: var(--text-muted); margin-top: 4px;">Number of consecutive valid readings required (~35ms per sample).</div>
      </div>

      <!-- Status LED Polarity (External LED on D4 vs Onboard Module) -->
      <div class="form-group">
        <div class="form-label">
          <span>Status LED Wiring Mode</span>
          <span class="val-tag" id="valLedPolarity">External LED (HIGH = ON)</span>
        </div>
        <div class="chips-row">
          <div class="chip" id="chipLedHigh" onclick="setLedPolarity(1)">External LED (HIGH = ON) [Default]</div>
          <div class="chip" id="chipLedLow" onclick="setLedPolarity(0)">Onboard Inverted (LOW = ON)</div>
        </div>
        <input type="hidden" id="inputLedHigh" value="1">
        <div style="font-size: 11px; color: var(--text-muted); margin-top: 4px;">External LEDs wired to D4 and GND use HIGH = ON. Change only if using Active-LOW wiring.</div>
      </div>

      <!-- Save & Reset Buttons -->
      <button class="btn-save" onclick="saveSettingsToEEPROM()">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M17 3H5c-1.11 0-2 .9-2 2v14c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2V7l-4-4zm-5 16c-1.66 0-3-1.34-3-3s1.34-3 3-3 3 1.34 3 3-1.34 3-3 3zm3-10H5V5h10v4z"/></svg>
        <span>SAVE SETTINGS TO EEPROM</span>
      </button>
      <button class="btn-reset" onclick="resetFactoryDefaults()">Reset to Factory Defaults</button>
    </div>

    <!-- Diagnostics Card -->
    <div class="card">
      <div class="card-header">
        <div>Diagnostics & Stats</div>
        <span>[Live Telemetry]</span>
      </div>
      <div class="stats-grid">
        <div class="stat-box">
          <div class="stat-val" id="statDispenses">0</div>
          <div class="stat-lbl">Total Dispenses</div>
        </div>
        <div class="stat-box">
          <div class="stat-val" id="statUptime">00:00</div>
          <div class="stat-lbl">System Uptime</div>
        </div>
        <div class="stat-box">
          <div class="stat-val" id="statClients">0</div>
          <div class="stat-lbl">Hotspot Clients</div>
        </div>
        <div class="stat-box">
          <div class="stat-val" id="statHeap">-- KB</div>
          <div class="stat-lbl">Free System RAM</div>
        </div>
      </div>
      <div style="margin-top: 10px; text-align: center;">
        <button class="btn-reset" style="color: var(--text-muted); font-size: 11px;" onclick="resetTotalCounter()">Clear Total Dispenses Counter</button>
      </div>
    </div>

    <!-- Settings & Calibration Info Guide Card -->
    <div class="card">
      <div class="card-header" style="cursor: pointer;" onclick="toggleGuide()">
        <div style="display: flex; align-items: center; gap: 8px;">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="var(--primary)"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z"/></svg>
          <div>Calibration & Settings Guide</div>
        </div>
        <button class="guide-toggle" id="guideToggleBtn">Hide Guide ▲</button>
      </div>

      <div id="guideContent">
        <div class="guide-item">
          <div class="guide-title">
            <span>🛡️ Mid-Dispense Hand Check</span>
            <span class="guide-badge">Safety Cutoff</span>
          </div>
          <div class="guide-desc">
            <b>Continuous Hand Monitoring:</b> The sensor continuously checks for hand presence while motor is dispensing. If a devotee removes their hand mid-dispense, the motor stops immediately to prevent prasad from falling on the floor.
          </div>
        </div>

        <div class="guide-item">
          <div class="guide-title">
            <span>💡 Dynamic Status LED Patterns</span>
            <span class="guide-badge">Visual Cues</span>
          </div>
          <div class="guide-desc">
            <b>Ready:</b> Solid Steady Glow (Machine ready for devotee).<br>
            <b>Dispensing:</b> Fast Energetic Blink (80ms strobe while motor runs).<br>
            <b>Waiting (Removal):</b> Moderate Blink (200ms prompt to remove hand).<br>
            <b>Waiting (Cooldown):</b> Slow Pulse (400ms pause before re-arming).<br>
            <b>Ready Again:</b> Returns to Solid Steady Glow.
          </div>
        </div>

        <div class="guide-item">
          <div class="guide-title">
            <span>🖐️ Trigger Distance & Removal Gap</span>
            <span class="guide-badge">Hysteresis</span>
          </div>
          <div class="guide-desc">
            <b>Trigger Point:</b> Motor initiates when hand is detected closer than the trigger distance (&lt; 16.0 cm).<br>
            <b>Reset Point:</b> Devotee must pull hand back beyond <i>Trigger + Hysteresis</i> (&gt; 18.0 cm). This 2.0 cm safety deadband prevents double-dosing if fingers twitch.
          </div>
        </div>

        <div class="guide-item">
          <div class="guide-title">
            <span>⏱️ Hand Settle Delay</span>
            <span class="guide-badge">Hand Cupping</span>
          </div>
          <div class="guide-desc">
            Pause after hand is detected before the motor starts (default: 100 ms). Gives devotee time to cup and position their hands under the nozzle. If hand is pulled away during this delay, dispensing safely aborts. Set to 0ms for instant motor spin.
          </div>
        </div>

        <div class="guide-item">
          <div class="guide-title">
            <span>⚡ Motor Run Duration</span>
            <span class="guide-badge">Portion Tuning</span>
          </div>
          <div class="guide-desc">
            Time in milliseconds the N20 motor runs at full 100% digital torque. Calibrate this timing so the cereal dispenser paddle advances by exactly <b>one blade pocket</b> per dispense (default: 1800 ms).
          </div>
        </div>

        <div class="guide-item">
          <div class="guide-title">
            <span>⏳ Devotee Cooldown</span>
            <span class="guide-badge">Anti-Retrigger</span>
          </div>
          <div class="guide-desc">
            Mandatory pause between devotees (default: 1500 ms). The timer <b>only starts after</b> the devotee leaves their hand. If they place their hand back before the cooldown ends, the timer resets to zero. Machine never re-dispenses while a hand remains under the sensor.
          </div>
        </div>

        <div class="guide-item">
          <div class="guide-title">
            <span>🛡️ Sensor Debounce Filter</span>
            <span class="guide-badge">Optical Filter</span>
          </div>
          <div class="guide-desc">
            Number of consecutive valid readings required (~35ms per sample, default: 3 samples = ~105ms). Eliminates optical false triggers caused by ambient sunlight, dust, or laser ToF reflections.
          </div>
        </div>
      </div>
    </div>
  </div>

  <div class="toast" id="toast">Settings Saved!</div>

  <script>
    let isEditing = false;
    let isFetching = false;

    function showToast(msg) {
      const toast = document.getElementById('toast');
      toast.innerText = msg;
      toast.classList.add('show');
      setTimeout(() => toast.classList.remove('show'), 2500);
    }

    function setDuration(ms) {
      document.getElementById('inputDuration').value = ms;
      updateUIValues();
    }

    function setTriggerDist(cm) {
      document.getElementById('inputTriggerDist').value = cm;
      updateUIValues();
    }

    function setCooldown(ms) {
      document.getElementById('inputCooldown').value = ms;
      updateUIValues();
    }

    function setPreDelay(ms) {
      document.getElementById('inputPreDelay').value = ms;
      updateUIValues();
    }

    function setLedPolarity(val) {
      document.getElementById('inputLedHigh').value = val;
      updateUIValues();
    }

    function adjustInput(id, delta) {
      const el = document.getElementById(id);
      let step = parseFloat(el.step) || 1;
      let min = parseFloat(el.min);
      let max = parseFloat(el.max);
      let val = parseFloat(el.value) + delta;
      if (val < min) val = min;
      if (val > max) val = max;
      
      if (step < 1) {
        el.value = val.toFixed(1);
      } else {
        el.value = Math.round(val);
      }
      updateUIValues();
    }

    function updateUIValues() {
      const trig = parseFloat(document.getElementById('inputTriggerDist').value) || 16.0;
      const hyst = parseFloat(document.getElementById('inputHysteresis').value) || 2.0;
      const dur = parseInt(document.getElementById('inputDuration').value) || 1800;
      const cld = parseInt(document.getElementById('inputCooldown').value) || 1500;
      const pre = parseInt(document.getElementById('inputPreDelay').value) || 100;
      const smp = parseInt(document.getElementById('inputSamples').value) || 3;
      const ledHigh = parseInt(document.getElementById('inputLedHigh').value) || 0;
      const resetDist = trig + hyst;

      document.getElementById('valTriggerDist').innerText = trig.toFixed(1) + ' cm';
      document.getElementById('valHysteresis').innerText = '+' + hyst.toFixed(1) + ' cm (Reset at ' + resetDist.toFixed(1) + ' cm)';
      document.getElementById('valDuration').innerText = dur + ' ms';
      document.getElementById('valCooldown').innerText = cld + ' ms';
      document.getElementById('valPreDelay').innerText = pre + ' ms';
      document.getElementById('valSamples').innerText = smp + ' Samples';
      document.getElementById('valLedPolarity').innerText = ledHigh ? 'External LED (HIGH = ON)' : 'Onboard Inverted (LOW = ON)';

      const chipH = document.getElementById('chipLedHigh');
      const chipL = document.getElementById('chipLedLow');
      if (chipH) {
        chipH.style.borderColor = ledHigh ? 'var(--primary)' : 'rgba(255,255,255,0.1)';
        chipH.style.color = ledHigh ? '#fff' : 'var(--text-muted)';
      }
      if (chipL) {
        chipL.style.borderColor = !ledHigh ? 'var(--primary)' : 'rgba(255,255,255,0.1)';
        chipL.style.color = !ledHigh ? '#fff' : 'var(--text-muted)';
      }

      document.getElementById('radarTriggerVal').innerText = 'Trigger < ' + trig.toFixed(1) + ' cm';
      document.getElementById('radarResetVal').innerText = 'Reset > ' + resetDist.toFixed(1) + ' cm';
      document.getElementById('legendTriggerText').innerText = '🔴 Trigger: < ' + trig.toFixed(1) + ' cm';
      document.getElementById('legendResetText').innerText = '🔵 Reset: > ' + resetDist.toFixed(1) + ' cm';

      // Update both line positions on the 0-30cm scale
      let trigPct = (trig / 30.0) * 100;
      if (trigPct > 96) trigPct = 96;
      if (trigPct < 4) trigPct = 4;
      document.getElementById('thresholdTriggerLine').style.left = trigPct + '%';

      let resetPct = (resetDist / 30.0) * 100;
      if (resetPct > 98) resetPct = 98;
      if (resetPct < 4) resetPct = 4;
      document.getElementById('thresholdResetLine').style.left = resetPct + '%';
    }

    function updateStatusUI(data) {
      // 1. Continuous Distance Update
      const distNum = document.getElementById('liveDistNum');
      const distFill = document.getElementById('distFill');

      if (!data.sensorOk) {
        distNum.innerText = 'OFFLINE';
        distNum.style.fontSize = '26px';
        distNum.style.color = '#ef4444';
        distFill.style.width = '0%';
      } else if (data.distanceCm >= 200 || data.distanceMm >= 8000) {
        distNum.innerText = '> 30';
        distNum.style.fontSize = '38px';
        distNum.style.color = 'var(--text-muted)';
        distFill.style.width = '100%';
      } else {
        distNum.innerText = data.distanceCm.toFixed(1);
        distNum.style.fontSize = '38px';
        distNum.style.color = (data.distanceCm <= data.triggerDist) ? '#f59e0b' : '#10b981';
        
        // Visual progress bar scaled 0 to 30 cm
        let barPct = (data.distanceCm / 30.0) * 100;
        if (barPct > 100) barPct = 100;
        if (barPct < 2) barPct = 2;
        distFill.style.width = barPct + '%';
      }

      // 2. Real-Time Motor Running Feedback
      const motorBadge = document.getElementById('hwMotorBadge');
      const motorState = document.getElementById('hwMotorState');
      if (data.motorActive) {
        motorBadge.classList.add('active-motor');
        motorState.innerText = 'RUNNING (' + data.motorDuration + 'ms)';
        motorState.style.color = '#f59e0b';
      } else {
        motorBadge.classList.remove('active-motor');
        motorState.innerText = 'STOPPED (OFF)';
        motorState.style.color = '#fff';
      }

      // 3. Real-Time LED On/Off Feedback
      const ledBadge = document.getElementById('hwLedBadge');
      const ledState = document.getElementById('hwLedState');
      if (data.state === 'IDLE') {
        ledBadge.classList.add('active-led');
        ledState.innerText = 'STEADY GLOW (READY)';
        ledState.style.color = '#10b981';
      } else if (data.state === 'DISPENSING') {
        ledBadge.classList.toggle('active-led', data.ledActive);
        ledState.innerText = 'FAST BLINK (DISPENSING)';
        ledState.style.color = '#f59e0b';
      } else if (data.state === 'WAIT_REMOVAL') {
        ledBadge.classList.toggle('active-led', data.ledActive);
        ledState.innerText = 'BLINK (REMOVE HAND)';
        ledState.style.color = '#38bdf8';
      } else if (data.state === 'COOLDOWN') {
        ledBadge.classList.toggle('active-led', data.ledActive);
        ledState.innerText = 'SLOW BLINK (COOLDOWN)';
        ledState.style.color = '#38bdf8';
      } else if (data.state === 'SETTLE') {
        ledBadge.classList.toggle('active-led', data.ledActive);
        ledState.innerText = 'BLINK (SETTLING)';
        ledState.style.color = '#f59e0b';
      } else {
        ledBadge.classList.remove('active-led');
        ledState.innerText = 'LED OFF';
        ledState.style.color = 'var(--text-muted)';
      }

      // 4. State Banner
      const dot = document.getElementById('statusDot');
      const title = document.getElementById('statusTitle');
      const sub = document.getElementById('statusSub');

      dot.className = 'status-dot';

      if (!data.sensorOk) {
        dot.classList.add('offline');
        title.innerText = 'SENSOR OFFLINE';
        sub.innerText = 'Check VL53L0X I2C wiring (D1/D2)';
      } else if (data.state === 'SETTLE') {
        dot.classList.add('dispensing');
        title.innerText = 'HAND SETTLING (' + data.preDelay + 'ms)...';
        sub.innerText = 'Positioning hand inside machine before motor starts';
      } else if (data.state === 'DISPENSING') {
        dot.classList.add('dispensing');
        title.innerText = 'DISPENSING PRASAD...';
        sub.innerText = 'N20 Motor running (' + data.motorDuration + 'ms) | Continuous hand monitoring active';
      } else if (data.state === 'WAIT_REMOVAL') {
        dot.classList.add('waiting');
        title.innerText = 'REMOVE HAND TO RESET';
        sub.innerText = 'Pull hand back past ' + data.resetDist.toFixed(1) + ' cm to start cooldown';
      } else if (data.state === 'COOLDOWN') {
        dot.classList.add('waiting');
        if (data.abortedEarly) {
          title.innerText = 'HAND REMOVED (CUT OFF)';
          sub.innerText = 'Hand removed mid-dispense! Motor cut off immediately. Cooldown: ' + data.cooldownRemMs + ' ms';
        } else {
          title.innerText = 'COOLDOWN ACTIVE';
          sub.innerText = 'Pause before next devotee (' + data.cooldownRemMs + ' ms remaining)';
        }
      } else {
        dot.classList.add('idle');
        title.innerText = 'READY (IDLE)';
        sub.innerText = 'Place hand within ' + data.triggerDist.toFixed(1) + ' cm';
      }

      // 5. Cooldown Countdown Feedback
      const cldBanner = document.getElementById('cooldownBanner');
      const cldLabel = document.getElementById('cooldownBannerLabel');
      const cldVal = document.getElementById('cooldownTimerVal');
      if (data.state === 'COOLDOWN' && data.cooldownRemMs > 0) {
        cldBanner.style.display = 'flex';
        if (data.abortedEarly) {
          cldBanner.style.background = 'rgba(239, 68, 68, 0.15)';
          cldBanner.style.borderColor = 'rgba(239, 68, 68, 0.4)';
          cldBanner.style.color = '#f87171';
          if (cldLabel) cldLabel.innerText = '⚠️ Hand Removed Mid-Dispense:';
          cldVal.innerText = 'Motor Cut Off (' + data.cooldownRemMs + ' ms cooldown)';
        } else {
          cldBanner.style.background = 'rgba(56, 189, 248, 0.12)';
          cldBanner.style.borderColor = 'rgba(56, 189, 248, 0.3)';
          cldBanner.style.color = '#38bdf8';
          if (cldLabel) cldLabel.innerText = '⏱️ Cooldown Active:';
          cldVal.innerText = data.cooldownRemMs + ' ms remaining';
        }
      } else if (data.state === 'WAIT_REMOVAL') {
        cldBanner.style.display = 'flex';
        cldBanner.style.background = 'rgba(245, 158, 11, 0.12)';
        cldBanner.style.borderColor = 'rgba(245, 158, 11, 0.3)';
        cldBanner.style.color = '#f59e0b';
        if (cldLabel) cldLabel.innerText = '✋ Hand Detected:';
        cldVal.innerText = 'Remove hand to start cooldown';
      } else {
        cldBanner.style.display = 'none';
      }

      // 6. Diagnostics & Counters
      document.getElementById('totalDispensesTop').innerText = data.totalDispenses;
      document.getElementById('statDispenses').innerText = data.totalDispenses;
      document.getElementById('statUptime').innerText = formatUptime(data.uptimeSec);
      if (document.getElementById('statClients')) document.getElementById('statClients').innerText = data.wifiClients;
      if (document.getElementById('statHeap')) document.getElementById('statHeap').innerText = data.freeHeapKb + ' KB';

      // 7. Sync Form Inputs (only if user is not actively editing)
      if (!isEditing) {
        document.getElementById('inputTriggerDist').value = data.triggerDist.toFixed(1);
        document.getElementById('inputHysteresis').value = data.hysteresis.toFixed(1);
        document.getElementById('inputDuration').value = data.motorDuration;
        document.getElementById('inputCooldown').value = data.cooldown;
        if (document.getElementById('inputPreDelay')) {
          document.getElementById('inputPreDelay').value = data.preDelay;
        }
        document.getElementById('inputSamples').value = data.samples;
        if (document.getElementById('inputLedHigh') && data.ledActiveHigh !== undefined) {
          document.getElementById('inputLedHigh').value = data.ledActiveHigh;
        }
        updateUIValues();
      }
    }

    function formatUptime(sec) {
      let m = Math.floor(sec / 60);
      let s = sec % 60;
      let h = Math.floor(m / 60);
      m = m % 60;
      if (h > 0) {
        return h + 'h ' + (m < 10 ? '0' : '') + m + 'm';
      }
      return (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
    }

    // Recursive fetch loop: avoids concurrent network requests
    function scheduleNextFetch() {
      setTimeout(fetchStatus, 200);
    }

    function fetchStatus() {
      if (isFetching) return;
      isFetching = true;

      fetch('/status')
        .then(res => res.json())
        .then(data => {
          updateStatusUI(data);
          isFetching = false;
          scheduleNextFetch();
        })
        .catch(err => {
          document.getElementById('statusTitle').innerText = 'RECONNECTING...';
          isFetching = false;
          scheduleNextFetch();
        });
    }

    function triggerManualDispense() {
      fetch('/trigger')
        .then(res => res.json())
        .then(data => {
          showToast('Manual Dispense Started!');
          updateStatusUI(data);
        })
        .catch(e => showToast('Command failed'));
    }

    function emergencyStopMotor() {
      fetch('/stop')
        .then(res => res.json())
        .then(data => {
          showToast('Motor Cut Off!');
          updateStatusUI(data);
        })
        .catch(e => showToast('Stop failed'));
    }

    function saveSettingsToEEPROM() {
      const trig = parseFloat(document.getElementById('inputTriggerDist').value);
      const hyst = parseFloat(document.getElementById('inputHysteresis').value);
      const dur  = parseInt(document.getElementById('inputDuration').value);
      const cld  = parseInt(document.getElementById('inputCooldown').value);
      const pre  = parseInt(document.getElementById('inputPreDelay').value);
      const smp  = parseInt(document.getElementById('inputSamples').value);
      const ledH = parseInt(document.getElementById('inputLedHigh').value) || 0;

      const url = `/save?trig=${trig}&hyst=${hyst}&dur=${dur}&cld=${cld}&pre=${pre}&smp=${smp}&ledHigh=${ledH}`;
      fetch(url)
        .then(res => res.json())
        .then(data => {
          showToast('Settings Saved to EEPROM!');
          isEditing = false;
          updateStatusUI(data);
        })
        .catch(e => showToast('Save failed'));
    }

    function resetFactoryDefaults() {
      if (!confirm('Reset all parameters to factory defaults?')) return;
      fetch('/resetDefaults')
        .then(res => res.json())
        .then(data => {
          showToast('Reset to Defaults Done!');
          isEditing = false;
          updateStatusUI(data);
        });
    }

    function resetTotalCounter() {
      if (!confirm('Clear total dispense counter to 0?')) return;
      fetch('/resetCount')
        .then(res => res.json())
        .then(data => {
          showToast('Counter Reset to 0!');
          updateStatusUI(data);
        });
    }

    function toggleGuide() {
      const el = document.getElementById('guideContent');
      const btn = document.getElementById('guideToggleBtn');
      if (el.style.display === 'none') {
        el.style.display = 'block';
        btn.innerText = 'Hide Guide ▲';
      } else {
        el.style.display = 'none';
        btn.innerText = 'Show Guide ▼';
      }
    }

    // Flag when user is typing in inputs to prevent polling overwrites
    const inputs = document.querySelectorAll('input');
    inputs.forEach(input => {
      input.addEventListener('focus', () => { isEditing = true; });
    });

    // Start live status polling
    updateUIValues();
    fetchStatus();
  </script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------------
// HTTP REQUEST HANDLERS
// ---------------------------------------------------------------------------------

// Send system status in JSON
void sendStatusJson() {
  String stateStr = "IDLE";
  if (currentState == STATE_SETTLE) {
    stateStr = "SETTLE";
  } else if (currentState == STATE_DISPENSING) {
    stateStr = "DISPENSING";
  } else if (currentState == STATE_WAIT_FOR_REMOVAL) {
    stateStr = "WAIT_REMOVAL";
  } else if (currentState == STATE_COOLDOWN) {
    stateStr = "COOLDOWN";
  }

  // Calculate cooldown remaining time
  unsigned long cooldownRemMs = 0;
  if (currentState == STATE_COOLDOWN) {
    unsigned long elapsed = millis() - cooldownStartTime;
    if (elapsed < config.cooldownMs) {
      cooldownRemMs = config.cooldownMs - elapsed;
    }
  } else if (currentState == STATE_WAIT_FOR_REMOVAL) {
    cooldownRemMs = config.cooldownMs;
  }

  float resetDist = config.triggerDistanceCm + config.hysteresisCm;

  String json;
  json.reserve(512); // Pre-reserve to eliminate heap fragmentation
  json = "{";
  json += "\"state\":\"" + stateStr + "\",";
  json += "\"distanceCm\":" + String(currentDistanceCm, 1) + ",";
  json += "\"distanceMm\":" + String(currentDistanceMm) + ",";
  json += "\"triggerDist\":" + String(config.triggerDistanceCm, 1) + ",";
  json += "\"hysteresis\":" + String(config.hysteresisCm, 1) + ",";
  json += "\"resetDist\":" + String(resetDist, 1) + ",";
  json += "\"motorDuration\":" + String(config.motorDurationMs) + ",";
  json += "\"cooldown\":" + String(config.cooldownMs) + ",";
  json += "\"cooldownRemMs\":" + String(cooldownRemMs) + ",";
  json += "\"preDelay\":" + String(config.preDispenseDelayMs) + ",";
  json += "\"samples\":" + String(config.triggerSamples) + ",";
  json += "\"totalDispenses\":" + String(config.totalDispenses) + ",";
  json += "\"uptimeSec\":" + String(millis() / 1000) + ",";
  json += "\"wifiClients\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"freeHeapKb\":" + String(ESP.getFreeHeap() / 1024.0, 1) + ",";
  json += "\"motorActive\":" + String(isMotorRunning() ? "true" : "false") + ",";
  json += "\"ledActive\":" + String(isStatusLedOn() ? "true" : "false") + ",";
  json += "\"abortedEarly\":" + String(dispenseAbortedEarly ? "true" : "false") + ",";
  json += "\"ledActiveHigh\":" + String(config.ledActiveHigh ? 1 : 0) + ",";
  json += "\"handPresent\":" + String((currentDistanceCm <= config.triggerDistanceCm) ? "true" : "false") + ",";
  json += "\"sensorOk\":" + String(sensorAvailable ? "true" : "false");
  json += "}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// Serve main web page
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

// Manual dispense trigger (from Web UI)
void handleTrigger() {
  isManualTest = true;
  triggerDispense();
  sendStatusJson();
}

// Emergency stop
void handleStop() {
  emergencyStop();
  sendStatusJson();
}

// Save settings to EEPROM
void handleSave() {
  if (server.hasArg("trig")) config.triggerDistanceCm  = server.arg("trig").toFloat();
  if (server.hasArg("hyst")) config.hysteresisCm       = server.arg("hyst").toFloat();
  if (server.hasArg("dur"))  config.motorDurationMs    = server.arg("dur").toInt();
  if (server.hasArg("cld"))     config.cooldownMs         = server.arg("cld").toInt();
  if (server.hasArg("pre"))     config.preDispenseDelayMs = server.arg("pre").toInt();
  if (server.hasArg("smp"))     config.triggerSamples     = server.arg("smp").toInt();
  if (server.hasArg("ledHigh")) config.ledActiveHigh      = server.arg("ledHigh").toInt() > 0 ? 1 : 0;

  // Validate bounds
  if (config.triggerDistanceCm < 2.0)   config.triggerDistanceCm = 2.0;
  if (config.triggerDistanceCm > 50.0)  config.triggerDistanceCm = 50.0;
  if (config.hysteresisCm < 0.2)        config.hysteresisCm = 0.2;
  if (config.hysteresisCm > 20.0)       config.hysteresisCm = 20.0;
  if (config.motorDurationMs < 100)     config.motorDurationMs = 100;
  if (config.motorDurationMs > 10000)   config.motorDurationMs = 10000;
  if (config.cooldownMs < 100)          config.cooldownMs = 100;
  if (config.cooldownMs > 10000)        config.cooldownMs = 10000;
  if (config.preDispenseDelayMs > 3000) config.preDispenseDelayMs = 3000;
  if (config.triggerSamples < 1)        config.triggerSamples = 1;
  if (config.triggerSamples > 15)       config.triggerSamples = 15;

  saveConfigToEEPROM();
  sendStatusJson();
}

// Reset cumulative counter
void handleResetCount() {
  config.totalDispenses = 0;
  saveConfigToEEPROM();
  sendStatusJson();
}

// Reset factory defaults
void handleResetDefaults() {
  setFactoryDefaults();
  saveConfigToEEPROM();
  sendStatusJson();
}

// Captive Portal Redirect Handler
void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------------
// ARDUINO SETUP & LOOP
// ---------------------------------------------------------------------------------
void setup() {
  Serial.begin(74880);
  delay(200);
  Serial.println();
  Serial.println("==================================================");
  Serial.println("   PRASAD DISPENSER V3 - ESP8266 CONTROLLER       ");
  Serial.println("==================================================");

  // Configure output pins
  pinMode(PIN_MOTOR, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  stopMotor();
  setStatusLed(true); // Turn on LED to indicate powering up

  // Load saved configuration from EEPROM
  loadConfigFromEEPROM();

  // Initialize VL53L0X Time-of-Flight sensor
  sensorAvailable = initializeSensor();
  if (sensorAvailable) {
    Serial.println("[Init] Sensor ready.");
  } else {
    Serial.println("[Init] Sensor warning: could not initialize! Check wiring.");
  }

  // Pre-fill distance moving average buffer
  for (int i = 0; i < SAMPLES_BUFFER_SIZE; i++) {
    distanceBuffer[i] = 100.0;
  }

  // Configure and start Wi-Fi Soft AP (Hotspot)
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(config.apSsid, config.apPassword);

  Serial.print("[WiFi] Hotspot SSID: ");
  Serial.println(config.apSsid);
  Serial.print("[WiFi] IP Address: ");
  Serial.println(WiFi.softAPIP());

  // Setup DNS server for Captive Portal (redirects all requests to ESP IP)
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  // Setup mDNS responder (http://prasad.local)
  if (MDNS.begin("prasad")) {
    Serial.println("[mDNS] http://prasad.local enabled");
  }

  // Setup Web Server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, sendStatusJson);
  server.on("/trigger", HTTP_GET, handleTrigger);
  server.on("/stop", HTTP_GET, handleStop);
  server.on("/save", HTTP_GET, handleSave);
  server.on("/resetCount", HTTP_GET, handleResetCount);
  server.on("/resetDefaults", HTTP_GET, handleResetDefaults);

  // Common Captive Portal detection probes (Android / iOS / Windows)
  server.on("/generate_204", HTTP_GET, handleRoot);
  server.on("/gen_204", HTTP_GET, handleRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  server.on("/ncsi.txt", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[Server] HTTP Web Server and Captive Portal ready!");
  setStatusLed(true); // LED ON indicates system is operational
}

void loop() {
  // 1. Process Captive Portal DNS & HTTP Web Server requests
  dnsServer.processNextRequest();
  server.handleClient();
  MDNS.update();

  // 2. Poll distance sensor (returns true ONLY when a fresh physical sample is ready)
  bool newSample = pollDistanceSensor();

  // 3. State Machine for Hand Detection and Prasad Dispense
  unsigned long now = millis();

  switch (currentState) {
    case STATE_IDLE: {
      // Idle state: LED pattern handled dynamically by updateLedPattern()

      // Only evaluate sensor debounce when a fresh hardware sample was taken
      if (newSample) {
        if (sensorAvailable && currentDistanceCm > 0.0 && currentDistanceCm <= config.triggerDistanceCm) {
          consecutiveTriggerCount++;
          if (consecutiveTriggerCount >= config.triggerSamples) {
            consecutiveTriggerCount = 0;
            consecutiveRemovalCount = 0;
            isManualTest = false;
            dispenseAbortedEarly = false;
            if (config.preDispenseDelayMs > 0) {
              currentState = STATE_SETTLE;
              settleStartTime = millis();
              Serial.println("[Dispenser] Hand detected! Settle delay started (" + String(config.preDispenseDelayMs) + "ms)...");
            } else {
              triggerDispense(); // Transitions to STATE_DISPENSING immediately
            }
          }
        } else {
          consecutiveTriggerCount = 0;
        }
      }
      break;
    }

    case STATE_SETTLE: {
      // Settle delay: give devotee time to position/settle hands inside machine before motor starts
      stopMotor();

      // Check if devotee pulled hand away during settle delay (only on fresh hardware samples)
      if (newSample) {
        float resetThreshold = config.triggerDistanceCm + config.hysteresisCm;
        bool handIsFar = (!sensorAvailable || currentDistanceCm > resetThreshold || currentDistanceCm >= 200.0);

        if (handIsFar) {
          consecutiveRemovalCount++;
          if (consecutiveRemovalCount >= 2) {
            consecutiveRemovalCount = 0;
            consecutiveTriggerCount = 0;
            currentState = STATE_IDLE;
            Serial.println("[Dispenser] Hand removed during settle delay -> Aborted back to IDLE.");
            break;
          }
        } else {
          consecutiveRemovalCount = 0;
        }
      }

      // If settle delay has elapsed, start dispensing (evaluated continuously via millis)
      if (now - settleStartTime >= config.preDispenseDelayMs) {
        consecutiveRemovalCount = 0;
        consecutiveTriggerCount = 0;
        isManualTest = false;
        triggerDispense(); // Transitions to STATE_DISPENSING
      }
      break;
    }

    case STATE_DISPENSING: {
      // 1. Continuously verify hand presence during dispensing (Devotee automatic dispensing)
      // Only increments on genuine fresh physical samples (~35ms apart) to prevent false stops from noise
      if (newSample && !isManualTest) {
        float resetThreshold = config.triggerDistanceCm + config.hysteresisCm;
        bool handIsFar = (!sensorAvailable || currentDistanceCm > resetThreshold || currentDistanceCm >= 200.0);

        if (handIsFar) {
          consecutiveRemovalCount++;
          // Confirm hand removal with 2 consecutive physical readings (~70ms)
          if (consecutiveRemovalCount >= 2) {
            stopMotor(); // Cut motor power immediately!
            consecutiveRemovalCount = 0;
            consecutiveTriggerCount = 0;
            dispenseAbortedEarly = true;
            currentState = STATE_COOLDOWN;
            cooldownStartTime = now;
            Serial.println("[Dispenser] HAND REMOVED MID-DISPENSE! Motor stopped immediately.");
            Serial.println("[Dispenser] Cooldown timer started to prevent immediate re-trigger.");
            break;
          }
        } else {
          consecutiveRemovalCount = 0;
        }
      }

      // 2. Check if dispense duration has completed normally (evaluated continuously via millis)
      if (now - stateStartTime >= config.motorDurationMs) {
        stopMotor();          // Cut motor power immediately
        consecutiveRemovalCount = 0;
        consecutiveTriggerCount = 0;
        isManualTest = false;
        dispenseAbortedEarly = false;

        // Check if devotee has already removed hand during dispensing
        float resetThreshold = config.triggerDistanceCm + config.hysteresisCm;
        bool handIsFar = (!sensorAvailable || currentDistanceCm > resetThreshold || currentDistanceCm >= 200.0);

        if (handIsFar) {
          // Devotee already removed hand while motor was finishing
          currentState = STATE_COOLDOWN;
          cooldownStartTime = now;
          Serial.println("[Dispenser] Dispense duration complete. Motor stopped.");
          Serial.println("[Dispenser] Hand already removed -> Cooldown timer started.");
        } else {
          // Devotee still has hand placed under sensor
          currentState = STATE_WAIT_FOR_REMOVAL;
          Serial.println("[Dispenser] Dispense duration complete. Motor stopped.");
          Serial.println("[Dispenser] Waiting for devotee to leave hand before starting cooldown.");
        }
      }
      break;
    }

    case STATE_WAIT_FOR_REMOVAL: {
      // Motor is OFF. Devotee must leave/remove hand beyond (triggerDistanceCm + hysteresisCm)
      stopMotor();

      // Check for hand removal (only on fresh hardware samples)
      if (newSample) {
        float resetThreshold = config.triggerDistanceCm + config.hysteresisCm;
        bool handIsFar = (!sensorAvailable || currentDistanceCm > resetThreshold || currentDistanceCm >= 200.0);

        if (handIsFar) {
          consecutiveRemovalCount++;
          if (consecutiveRemovalCount >= 2) {
            consecutiveRemovalCount = 0;
            consecutiveTriggerCount = 0;
            currentState = STATE_COOLDOWN;
            cooldownStartTime = millis(); // Start cooldown timer now that hand is removed
            Serial.println("[Dispenser] Devotee left hand -> Starting cooldown timer.");
          }
        } else {
          consecutiveRemovalCount = 0; // Hand is still placed under sensor
        }
      }
      break;
    }

    case STATE_COOLDOWN: {
      // Motor is OFF. Cooldown timer is running.
      stopMotor();

      // Check if devotee placed hand again before cooldown is over (only on fresh hardware samples)
      if (newSample) {
        if (sensorAvailable && currentDistanceCm > 0.0 && currentDistanceCm <= config.triggerDistanceCm) {
          consecutiveTriggerCount++;
          if (consecutiveTriggerCount >= 2) {
            consecutiveTriggerCount = 0;
            consecutiveRemovalCount = 0;
            currentState = STATE_WAIT_FOR_REMOVAL; // Reset cooldown timer, wait for removal again
            Serial.println("[Dispenser] Hand placed again before cooldown finished! Cooldown timer reset.");
            break;
          }
        } else {
          consecutiveTriggerCount = 0;
        }
      }

      // Check if cooldown timer has completed (evaluated continuously via millis)
      if (now - cooldownStartTime >= config.cooldownMs) {
        float resetThreshold = config.triggerDistanceCm + config.hysteresisCm;
        bool handIsFar = (!sensorAvailable || currentDistanceCm > resetThreshold || currentDistanceCm >= 200.0);

        if (handIsFar) {
          currentState = STATE_IDLE;
          consecutiveTriggerCount = 0;
          consecutiveRemovalCount = 0;
          dispenseAbortedEarly = false;
          Serial.println("[Dispenser] Cooldown passed & hand clear. Ready for next devotee!");
        } else {
          // Hand is still within threshold, reset and wait for removal
          currentState = STATE_WAIT_FOR_REMOVAL;
          consecutiveRemovalCount = 0;
          consecutiveTriggerCount = 0;
          Serial.println("[Dispenser] Cooldown ended but hand not clear -> Waiting for hand removal.");
        }
      }
      break;
    }
  }

  // 4. Update dynamic hardware LED signaling pattern
  updateLedPattern();

  // 5. Periodic auto-save of dispense counter to EEPROM (wear-levelled)
  if (counterNeedsSave && (now - lastDispenseSaveTime > 15000 || config.totalDispenses % 10 == 0)) {
    counterNeedsSave = false;
    saveConfigToEEPROM();
    Serial.println("[EEPROM] Auto-saved total dispense counter to flash.");
  }

  // Prevent ESP8266 Software Watchdog reset
  yield();
}
