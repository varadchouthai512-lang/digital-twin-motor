"""
Digital Twin — OS-PRO AC Motor
Raspberry Pi Edge ML Predictor

Subscribes to live sensor data from ESP32 via MQTT,
runs trained Random Forest model for fault prediction,
and publishes predictions back to the MQTT broker.

Hardware: Raspberry Pi 3B / 4
Install:  pip install paho-mqtt scikit-learn numpy joblib

Usage:
    1. Train model first:  python train_model.py
    2. Run predictor:      python ml_predictor.py

MQTT:
    Subscribe: plantA/line1/motor_01/state
    Publish:   plantA/line1/motor_01/prediction

MIT-WPU | TY B.Tech EEE | 2025-26
"""

import paho.mqtt.client as mqtt
import json
import numpy as np
import time
import joblib
import os
from datetime import datetime

# ── Config ────────────────────────────────────────────────────
BROKER      = "localhost"          # Change to ESP32 broker IP if needed
PORT        = 1883
SUB_TOPIC   = "plantA/line1/motor_01/state"
PUB_TOPIC   = "plantA/line1/motor_01/prediction"
CLIENT_ID   = "RPi_ML_Predictor_01"
MODEL_PATH  = "models/motor_rf_model.joblib"

# ── Fault class labels ────────────────────────────────────────
LABELS = {
    0: "NORMAL",
    1: "WARN_TEMP",
    2: "WARN_OVERCURRENT",
    3: "WARN_VIBRATION",
    4: "CRITICAL_OVERHEAT",
    5: "FAULT_JAM"
}

# ── Load trained model ────────────────────────────────────────
model = None
if os.path.exists(MODEL_PATH):
    model = joblib.load(MODEL_PATH)
    print(f"[MODEL] Loaded: {MODEL_PATH}")
else:
    print(f"[MODEL] Not found at {MODEL_PATH}")
    print("[MODEL] Run train_model.py first, then copy models/ here")
    print("[MODEL] Falling back to rule-based prediction\n")


# ── Fallback: rule-based prediction ──────────────────────────
def rule_predict(temp, current, rpm, vib):
    if rpm < 10 and current > 0.30:
        return 5, "FAULT_JAM",         0.99
    if temp >= 70.0:
        return 4, "CRITICAL_OVERHEAT", 0.98
    if temp >= 55.0:
        return 1, "WARN_TEMP",         0.90
    if current > 0.65:
        return 2, "WARN_OVERCURRENT",  0.92
    if vib == 1:
        return 3, "WARN_VIBRATION",    0.85
    return     0, "NORMAL",            0.99


# ── Run prediction ────────────────────────────────────────────
def predict(data):
    temp    = float(data.get("temp",      0))
    current = float(data.get("current",   0))
    rpm     = float(data.get("rpm",       0))
    vib     = int  (data.get("vibration", 0))

    if model is not None:
        features = np.array([[temp, current, rpm, vib]])
        pred_id  = model.predict(features)[0]
        proba    = model.predict_proba(features)[0]
        label    = LABELS.get(pred_id, "UNKNOWN")
        conf     = round(float(max(proba)), 3)
        return pred_id, label, conf
    else:
        return rule_predict(temp, current, rpm, vib)


# ── MQTT ──────────────────────────────────────────────────────
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[MQTT] Connected to {BROKER}:{PORT}")
        client.subscribe(SUB_TOPIC)
        print(f"[MQTT] Subscribed: {SUB_TOPIC}")
        print(f"[MQTT] Publishing: {PUB_TOPIC}\n")
    else:
        print(f"[MQTT] Failed rc={rc}")


def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
        _, label, confidence = predict(data)

        result = {
            "timestamp":  int(time.time()),
            "asset_id":   "motor_01",
            "prediction": label,
            "confidence": confidence,
            "source":     "random_forest" if model else "rule_based",
            "temp":       data.get("temp"),
            "current":    data.get("current"),
            "rpm":        data.get("rpm"),
            "vibration":  data.get("vibration"),
        }

        client.publish(PUB_TOPIC, json.dumps(result))

        # Color output
        c = "\033[92m" if label == "NORMAL" \
            else "\033[93m" if "WARN" in label \
            else "\033[91m"
        r = "\033[0m"
        ts = datetime.now().strftime("%H:%M:%S")
        print(f"[{ts}] {c}{label:22s}{r}  conf={confidence:.2f}  "
              f"T={data.get('temp'):.1f}°C  "
              f"I={data.get('current'):.2f}A  "
              f"RPM={data.get('rpm'):.0f}")

    except Exception as e:
        print(f"[ERROR] {e}")


# ── Main ──────────────────────────────────────────────────────
def main():
    print("=" * 55)
    print("  OS-PRO Motor — Raspberry Pi Edge ML Predictor")
    print("  MIT-WPU TY B.Tech EEE 2025-26")
    print("=" * 55)
    print(f"  Broker : {BROKER}:{PORT}")
    print(f"  Model  : {'✅ RF loaded' if model else '⚠️  Rule-based fallback'}")
    print("=" * 55 + "\n")

    c = mqtt.Client(client_id=CLIENT_ID)
    c.on_connect = on_connect
    c.on_message = on_message

    try:
        c.connect(BROKER, PORT, 60)
        c.loop_forever()
    except KeyboardInterrupt:
        print("\n[EXIT] Stopped")
    finally:
        c.disconnect()


if __name__ == "__main__":
    main()
