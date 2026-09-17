# Digital Twin — OS-PRO AC Motor Predictive Maintenance

**MIT-WPU, Pune | Department of Electrical & Electronics Engineering | TY B.Tech | 2025–26**

A real-time **predictive maintenance system** for an OS-PRO single phase AC induction motor using digital twin architecture. Sensor data from the physical motor flows through a complete IoT pipeline — ESP32 → MQTT → InfluxDB → Grafana — with a 3D animated digital twin dashboard that mirrors the motor's live state.

<img width="1600" height="900" alt="WhatsApp Image 2026-09-17 at 00 12 51" src="https://github.com/user-attachments/assets/2f2fea0a-249c-4a84-b159-b7e8b5ae9885" />


---

## Hardware

| Component | Specification |
|---|---|
| Motor | OS-PRO HI-SPEED, Single Phase AC, 230V 50Hz, 2400 RPM, 19W |
| Microcontroller | ESP32 Dev Board (240MHz dual-core, built-in WiFi) |
| Current Sensor | ACS712-20A (Hall effect, 100mV/A, galvanic isolation) |
| Temperature Sensor | DS18B20 (1-Wire, ±0.5°C accuracy) |
| Vibration Sensor | SW-420 (D0 digital output) |
| RPM Sensor | A3144 Hall Effect + neodymium magnet on shaft |
| Motor Control | 5V Relay module (ON/OFF via ESP32 GPIO) |

---

## System Architecture

```
OS-PRO AC Motor (230V)
        │
        ├── DS18B20  (Temperature) ──→ GPIO 4
        ├── ACS712   (Current)     ──→ GPIO 34
        ├── A3144    (RPM)         ──→ GPIO 25  [Hardware ISR]
        ├── SW-420   (Vibration)   ──→ GPIO 26
        └── Relay    (Motor ON/OFF)──→ GPIO 32
                          │
                       ESP32
                    (Fault Logic)
                          │ WiFi
                       MQTT
                  (Mosquitto :1883)
                          │
                       Telegraf
                  (MQTT → InfluxDB)
                          │
                      InfluxDB v2
                  (Time-series DB)
                          │
            ┌─────────────┴─────────────┐
          Grafana                 Digital Twin
       (Live Dashboard)        (3D Visual Model)
                                      +
                             Raspberry Pi (Edge ML)  ← Future Phase
```

---

## Sensors & GPIO Map

| Sensor | GPIO | Parameter | Notes |
|---|---|---|---|
| DS18B20 | 4 | Temperature (°C) | 4.7kΩ pull-up to 3.3V required |
| ACS712-20A | 34 | Current (A) | ADC pin only; calibrate V0 at zero load |
| A3144 Hall | 25 | RPM | 10kΩ pull-up; IRAM_ATTR ISR |
| SW-420 | 26 | Vibration | Use D0 output |
| Relay | 32 | Motor ON/OFF | HIGH = motor on |

---

## Fault Detection

Six fault states using multi-condition logic — eliminates false positives:

| Status | Condition | Response |
|---|---|---|
| `NORMAL` | All parameters in range | Motor runs |
| `WARN_TEMP` | 55°C ≤ Temp < 70°C | Alert |
| `WARN_OVERCURRENT` | Current > 0.65A | Alert |
| `WARN_VIBRATION` | Vibration HIGH | Alert |
| `CRITICAL_OVERHEAT` | Temp ≥ 70°C | **Relay OFF** |
| `FAULT_JAM` | RPM < 10 AND Current > 0.3A | **Relay OFF** |

---

## MQTT Data Format

**Topic:** `plantA/line1/motor_01/state`

**Payload (JSON, 1 message/second):**
```json
{
  "temp": 42.30,
  "current": 0.521,
  "rpm": 2350.0,
  "vibration": 0,
  "motor_on": 1,
  "status": "NORMAL"
}
```

Remote control via: `plantA/line1/motor_01/cmd` → publish `ON` or `OFF`

---

## Project Structure

```
digital-twin-motor/
│
├── esp32/
│   └── motor_digital_twin/
│       └── motor_digital_twin.ino     ← ESP32 firmware (main code)
│
├── telegraf/
│   └── motor.conf                     ← Telegraf: MQTT → InfluxDB bridge
│
├── grafana/
│   └── dashboard.json                 ← Import this into Grafana
│
├── digital-twin/
│   └── index.html                     ← 3D animated digital twin (open in browser)
│
├── raspberry-pi/
│   ├── ml_predictor.py                ← Edge ML inference on Raspberry Pi
│   ├── train_model.py                 ← Train Random Forest on InfluxDB data
│   └── requirements.txt
│
├── .gitignore
└── README.md
```

---

## Setup Guide

### Prerequisites

- Arduino IDE + ESP32 board package
- [Mosquitto MQTT Broker](https://mosquitto.org/download/)
- [InfluxDB v2](https://www.influxdata.com/downloads/)
- [Telegraf](https://www.influxdata.com/time-series-platform/telegraf/)
- [Grafana](https://grafana.com/grafana/download)

---

### 1. Flash ESP32

Open `esp32/motor_digital_twin/motor_digital_twin.ino` in Arduino IDE.

Install libraries via Library Manager:
- `OneWire` by Jim Studt
- `DallasTemperature` by Miles Burton
- `PubSubClient` by Nick O'Leary

Update these lines with your details:
```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_SERVER   = "YOUR_LAPTOP_IP";   // run ipconfig to find it
```

Select: **Board → ESP32 Dev Module** → Upload.

---

### 2. Start Mosquitto

Create / edit `mosquitto.conf`:
```
listener 1883
allow_anonymous true
```

Run:
```bash
# Windows
mosquitto -v -c mosquitto.conf

# Linux
sudo systemctl start mosquitto
```

Verify messages arriving:
```bash
mosquitto_sub -h localhost -t "plantA/line1/motor_01/state" -v
```

---

### 3. Start InfluxDB

```bash
./influxd
```

Open `http://localhost:8086`, create:
- Organisation: `mit-wpu`
- Bucket: `motor_data`
- Generate API token → copy it

---

### 4. Run Telegraf

Replace token in `telegraf/motor.conf`:
```toml
token = "YOUR_INFLUXDB_TOKEN"
```

Run:
```bash
telegraf --config telegraf/motor.conf
```

---

### 5. Grafana Dashboard

1. Open `http://localhost:3000` (admin/admin)
2. Add InfluxDB data source:
   - Query Language: **Flux**
   - URL: `http://localhost:8086`
   - Organisation: `mit-wpu`
   - Token: your token
   - Default Bucket: `motor_data`
3. Import dashboard: **Dashboards → Import → Upload JSON** → select `grafana/dashboard.json`

---

### 6. Digital Twin

Open `digital-twin/index.html` directly in any browser.

Shows live animated OS-PRO motor:
- Shaft spins at speed proportional to real RPM
- Body color changes with temperature (cool → warn → critical)
- Vibration rings appear during fault
- Status badge updates every second
- Live trend charts for temp, current, RPM

---

## Raspberry Pi — Edge ML (Phase 2)

```bash
# Install dependencies on Raspberry Pi
pip install -r raspberry-pi/requirements.txt

# Collect data from InfluxDB → export CSV → place at:
#   raspberry-pi/data/motor_data.csv

# Train Random Forest model
python raspberry-pi/train_model.py

# Run live predictor (subscribes to MQTT, publishes predictions)
python raspberry-pi/ml_predictor.py
```

Predictions published to: `plantA/line1/motor_01/prediction`

**Architecture:**
```
ESP32 → MQTT → Raspberry Pi (RF model) → MQTT → Grafana/Dashboard
```

---

## Results

| Metric | Value |
|---|---|
| Fault detection accuracy | > 97% |
| MQTT latency | < 100 ms |
| Publish interval | 1 second |
| Physical → digital sync time | < 2 seconds end-to-end |
| False positive rate | ~0% (multi-condition logic) |

---

## Technology Stack

| Layer | Technology |
|---|---|
| Microcontroller | ESP32 (240MHz, dual-core, WiFi) |
| IoT Protocol | MQTT (QoS 0, PubSubClient library) |
| Broker | Eclipse Mosquitto |
| Data Bridge | Telegraf |
| Database | InfluxDB v2 (time-series) |
| Query Language | Flux |
| Dashboard | Grafana |
| Digital Twin | HTML5 Canvas + Chart.js |
| Edge ML | Python + scikit-learn (Raspberry Pi) |
| ML Algorithm | Random Forest Classifier |

---

## Future Scope

- Raspberry Pi edge ML inference integration
- LSTM neural network for time-series fault prediction
- Firebase cloud backend for remote access
- Mobile dashboard (React Native)
- Multi-motor fleet monitoring
- PCB design replacing breadboard
- AWS IoT / Azure IoT Hub cloud deployment

---

## References

1. Pliuhin, V. et al. (2025). *The State and Prospects of Technical Condition Diagnostics Methods for Electric Motors Considering the Digital Twin Concept.* Lighting Engineering & Power Engineering, 64(2), 57–64. https://doi.org/10.33042/2079-424X.2025.64.2.02
2. InfluxData. (2024). *InfluxDB v2 Documentation.* https://docs.influxdata.com
3. Eclipse Foundation. (2024). *Mosquitto MQTT Broker.* https://mosquitto.org

---

## License

MIT License — Free to use for educational and research purposes.
