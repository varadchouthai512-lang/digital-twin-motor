/*
 * ================================================================
 *  Digital Twin — OS-PRO AC Motor Predictive Maintenance
 *  MIT-WPU, Pune | TY B.Tech EEE | 2025-26
 * ================================================================
 *
 *  Motor:   OS-PRO Single Phase AC Induction Motor
 *           230V, 50Hz, 2400 RPM, 19W, 0.65A max
 *
 *  Sensors:
 *    DS18B20  → Temperature  → GPIO 4  (1-Wire)
 *    ACS712   → Current      → GPIO 34 (ADC)
 *    A3144    → RPM          → GPIO 25 (Hall / Interrupt)
 *    SW-420   → Vibration    → GPIO 26 (Digital D0)
 *    Relay    → Motor ON/OFF → GPIO 32
 *
 *  Data Pipeline:
 *    ESP32 → WiFi → MQTT → Mosquitto → Telegraf → InfluxDB → Grafana
 *
 *  MQTT Topic:
 *    Publish:   plantA/line1/motor_01/state
 *    Subscribe: plantA/line1/motor_01/cmd
 *
 *  Libraries required (install via Arduino Library Manager):
 *    - OneWire          by Jim Studt
 *    - DallasTemperature by Miles Burton
 *    - PubSubClient     by Nick O'Leary
 * ================================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ── Pin Definitions ──────────────────────────────────────────
#define DS18B20_PIN   4     // Temperature sensor data line
#define VIB_PIN      26     // SW-420 vibration (D0 output)
#define HALL_PIN     25     // A3144 Hall sensor (RPM)
#define ACS_PIN      34     // ACS712 current sensor (ADC)
#define RELAY_PIN    32     // Relay module (motor ON/OFF)

// ── WiFi Credentials ─────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ── MQTT Settings ─────────────────────────────────────────────
const char* MQTT_SERVER   = "YOUR_LAPTOP_IP";   // e.g. "192.168.1.10"
const int   MQTT_PORT     = 1883;
const char* MQTT_CLIENT   = "ESP32_Motor_DT_01";
const char* MQTT_PUB      = "plantA/line1/motor_01/state";
const char* MQTT_SUB      = "plantA/line1/motor_01/cmd";

// ── ACS712-20A Calibration ────────────────────────────────────
// Run motor with no load and note the ADC voltage — set as V0
const float ACS_V0   = 2.37;   // Zero-current offset voltage (V)
const float ACS_SENS = 0.100;  // Sensitivity: 100mV/A for 20A module

// ── Fault Thresholds ─────────────────────────────────────────
const float TEMP_WARN  = 55.0;  // °C — warning
const float TEMP_CRIT  = 70.0;  // °C — critical, motor stops
const float CURR_MAX   = 0.65;  // A  — rated max current
const float JAM_CURR   = 0.30;  // A  — jam detection threshold
const float JAM_RPM    = 10.0;  // RPM — jam detection threshold

// ── Objects ───────────────────────────────────────────────────
OneWire           oneWire(DS18B20_PIN);
DallasTemperature tempSensor(&oneWire);
WiFiClient        espClient;
PubSubClient      mqttClient(espClient);

// ── RPM Counter ───────────────────────────────────────────────
volatile unsigned long hallPulses = 0;
unsigned long lastRpmMillis       = 0;
float rpm                         = 0;

// ── State ─────────────────────────────────────────────────────
bool motorOn = false;

// ─────────────────────────────────────────────────────────────
//  INTERRUPT — Hall Sensor
//  Placed in IRAM so it fires even during WiFi/flash operations
// ─────────────────────────────────────────────────────────────
void IRAM_ATTR hallISR() {
  hallPulses++;
}

// ─────────────────────────────────────────────────────────────
//  MOTOR CONTROL
// ─────────────────────────────────────────────────────────────
void motorON() {
  digitalWrite(RELAY_PIN, HIGH);
  motorOn = true;
  Serial.println("[RELAY] Motor ON");
}

void motorOFF() {
  digitalWrite(RELAY_PIN, LOW);
  motorOn = false;
  Serial.println("[RELAY] Motor OFF — Fault triggered");
}

// ─────────────────────────────────────────────────────────────
//  SENSOR READS
// ─────────────────────────────────────────────────────────────
float readTemperature() {
  tempSensor.requestTemperatures();
  float t = tempSensor.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) {
    Serial.println("[WARN] DS18B20 disconnected");
    return -1.0;
  }
  return t;
}

float readCurrent() {
  // Oversample ADC 16x for better accuracy
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(ACS_PIN);
    delayMicroseconds(100);
  }
  float raw     = sum / 16.0;
  float voltage = raw * (3.3 / 4095.0);
  float amps    = (voltage - ACS_V0) / ACS_SENS;
  if (amps < 0) amps = 0;
  return amps;
}

float computeRPM() {
  unsigned long now = millis();
  if (now - lastRpmMillis < 1000) return rpm;   // Update every 1s

  noInterrupts();
  unsigned long p = hallPulses;
  hallPulses = 0;
  interrupts();

  rpm          = (float)p * 60.0;   // 1 magnet = 1 pulse/revolution
  lastRpmMillis = now;
  return rpm;
}

// ─────────────────────────────────────────────────────────────
//  WIFI
// ─────────────────────────────────────────────────────────────
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected  IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] FAILED — check SSID/password");
  }
}

// ─────────────────────────────────────────────────────────────
//  MQTT CALLBACK — receive remote commands
// ─────────────────────────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg = "";
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  Serial.println("[MQTT CMD] " + msg);
  if (msg == "ON")  motorON();
  if (msg == "OFF") motorOFF();
}

// ─────────────────────────────────────────────────────────────
//  MQTT RECONNECT
// ─────────────────────────────────────────────────────────────
void mqttReconnect() {
  int tries = 0;
  while (!mqttClient.connected() && tries < 5) {
    Serial.print("[MQTT] Connecting...");
    if (mqttClient.connect(MQTT_CLIENT)) {
      Serial.println("OK");
      mqttClient.subscribe(MQTT_SUB);
    } else {
      Serial.println("failed rc=" + String(mqttClient.state()) + " retry 2s");
      delay(2000);
      tries++;
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  PUBLISH SENSOR DATA AS JSON
// ─────────────────────────────────────────────────────────────
void publishState(float temp, float current, float rpmVal,
                  int vib, bool mOn, String status) {
  String payload = "{";
  payload += "\"temp\":"      + String(temp, 2)     + ",";
  payload += "\"current\":"   + String(current, 3)  + ",";
  payload += "\"rpm\":"       + String(rpmVal, 1)   + ",";
  payload += "\"vibration\":" + String(vib)         + ",";
  payload += "\"motor_on\":"  + String(mOn ? 1 : 0) + ",";
  payload += "\"status\":\""  + status              + "\"";
  payload += "}";

  bool ok = mqttClient.publish(MQTT_PUB, payload.c_str(), false);
  Serial.println("[MQTT] " + String(ok ? "✓" : "✗") + " " + payload);
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n========================================");
  Serial.println("  Digital Twin — OS-PRO AC Motor");
  Serial.println("  MIT-WPU TY B.Tech EEE 2025-26");
  Serial.println("========================================");

  // Relay
  pinMode(RELAY_PIN, OUTPUT);
  motorOFF();   // Safe start — motor off

  // Sensors
  tempSensor.begin();
  pinMode(VIB_PIN, INPUT);
  pinMode(HALL_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_PIN), hallISR, FALLING);
  lastRpmMillis = millis();

  // Network
  connectWiFi();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(30);

  // Start motor after everything is ready
  delay(2000);
  motorON();

  Serial.println("[SYSTEM] Ready — publishing every 1 second");
  Serial.println("========================================\n");
}

// ─────────────────────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  // Maintain MQTT connection
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) mqttReconnect();
    mqttClient.loop();
  }

  // ── Read sensors ──────────────────────────────────────────
  float temp    = readTemperature();
  float current = readCurrent();
  float rpmVal  = computeRPM();
  int   vib     = digitalRead(VIB_PIN);

  // ── Multi-condition fault detection ───────────────────────
  String status = "NORMAL";

  // Critical overheat → immediate shutdown
  if (temp >= TEMP_CRIT) {
    motorOFF();
    status = "CRITICAL_OVERHEAT";
  }
  // Temperature warning
  else if (temp >= TEMP_WARN) {
    status = "WARN_TEMP";
  }

  // Overcurrent
  if (current > CURR_MAX && status == "NORMAL") {
    status = "WARN_OVERCURRENT";
  }

  // Vibration (only flag if otherwise normal)
  if (vib == HIGH && status == "NORMAL") {
    status = "WARN_VIBRATION";
  }

  // JAM: low RPM + high current simultaneously
  if (rpmVal < JAM_RPM && current > JAM_CURR && motorOn) {
    motorOFF();
    status = "FAULT_JAM";
  }

  // ── Serial monitor output ─────────────────────────────────
  Serial.printf("T=%.1f°C  I=%.3fA  RPM=%.0f  Vib=%d  [%s]\n",
                temp, current, rpmVal, vib, status.c_str());

  // ── Publish to MQTT ───────────────────────────────────────
  publishState(temp, current, rpmVal, vib, motorOn, status);

  delay(1000);
}
