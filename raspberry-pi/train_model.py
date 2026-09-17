"""
Digital Twin — OS-PRO AC Motor
Random Forest Fault Classifier — Training Script

Steps:
  1. Export sensor data from InfluxDB as CSV
  2. Place file as: data/motor_data.csv
  3. Run: python train_model.py
  4. Model saved to: models/motor_rf_model.joblib
  5. Copy models/ to Raspberry Pi alongside ml_predictor.py

CSV columns expected:
  temp, current, rpm, vibration, status

MIT-WPU | TY B.Tech EEE | 2025-26
"""

import numpy as np
import pandas as pd
import os
import joblib
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split, cross_val_score
from sklearn.metrics import (classification_report,
                              accuracy_score,
                              confusion_matrix)
from sklearn.preprocessing import LabelEncoder

# ── Paths ─────────────────────────────────────────────────────
DATA_PATH  = "data/motor_data.csv"
MODEL_DIR  = "models"
MODEL_PATH = os.path.join(MODEL_DIR, "motor_rf_model.joblib")

# ── Label map ─────────────────────────────────────────────────
LABEL_MAP = {
    "NORMAL":            0,
    "WARN_TEMP":         1,
    "WARN_OVERCURRENT":  2,
    "WARN_VIBRATION":    3,
    "CRITICAL_OVERHEAT": 4,
    "FAULT_JAM":         5,
}

FEATURES = ["temp", "current", "rpm", "vibration"]


# ── Load data ─────────────────────────────────────────────────
def load_data():
    if not os.path.exists(DATA_PATH):
        raise FileNotFoundError(
            f"\n[ERROR] Data file not found: {DATA_PATH}\n"
            "  Steps to generate data:\n"
            "  1. Run the ESP32 firmware and collect sensor readings\n"
            "  2. In InfluxDB → Data Explorer → CSV export\n"
            "  3. Place CSV at: data/motor_data.csv\n"
            "  Columns needed: temp, current, rpm, vibration, status\n"
        )

    df = pd.read_csv(DATA_PATH)
    print(f"[DATA] Loaded {len(df)} samples from {DATA_PATH}")
    print(f"[DATA] Columns: {list(df.columns)}")
    print(f"\n[DATA] Class distribution:")
    print(df["status"].value_counts())
    return df


# ── Preprocess ────────────────────────────────────────────────
def preprocess(df):
    df = df.dropna(subset=FEATURES + ["status"])
    df = df[df["status"].isin(LABEL_MAP.keys())]
    df["label"] = df["status"].map(LABEL_MAP)

    X = df[FEATURES].values
    y = df["label"].values

    print(f"\n[PREP] {len(X)} samples after cleaning")
    return X, y


# ── Train ─────────────────────────────────────────────────────
def train(X, y):
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42, stratify=y
    )

    print("\n[TRAIN] Fitting Random Forest (100 trees)...")
    model = RandomForestClassifier(
        n_estimators=100,
        max_depth=12,
        min_samples_split=5,
        random_state=42,
        n_jobs=-1
    )
    model.fit(X_train, y_train)

    # Evaluate
    y_pred = model.predict(X_test)
    acc = accuracy_score(y_test, y_pred)

    print(f"\n[RESULT] Test accuracy: {acc * 100:.1f}%")
    print("\n[RESULT] Classification Report:")
    print(classification_report(y_test, y_pred,
                                target_names=list(LABEL_MAP.keys())))

    # Feature importance
    print("[RESULT] Feature Importances:")
    for f, imp in zip(FEATURES, model.feature_importances_):
        bar = "█" * int(imp * 50)
        print(f"  {f:12s} {bar}  {imp:.3f}")

    # Cross-validation
    cv = cross_val_score(model, X, y, cv=5, scoring="accuracy")
    print(f"\n[CV] 5-Fold CV accuracy: {cv.mean()*100:.1f}% ± {cv.std()*100:.1f}%")

    return model


# ── Save ──────────────────────────────────────────────────────
def save(model):
    os.makedirs(MODEL_DIR, exist_ok=True)
    joblib.dump(model, MODEL_PATH)
    size = os.path.getsize(MODEL_PATH) / 1024
    print(f"\n[SAVE] Model saved → {MODEL_PATH} ({size:.1f} KB)")
    print("[SAVE] Copy models/ folder to Raspberry Pi")
    print("[SAVE] Then run: python ml_predictor.py")


# ── Main ──────────────────────────────────────────────────────
if __name__ == "__main__":
    print("=" * 55)
    print("  OS-PRO Motor — ML Model Training")
    print("  MIT-WPU TY B.Tech EEE 2025-26")
    print("=" * 55 + "\n")

    df    = load_data()
    X, y  = preprocess(df)
    model = train(X, y)
    save(model)
