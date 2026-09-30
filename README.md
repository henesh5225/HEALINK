# HEALINK

**Fault-Aware, Offline-First Edge-AI Personal Health Companion for Smart India Hackathon 2026 — Problem Statement #26181.**

HEALINK is a wearable/edge health monitoring system built around the **IndusBoard Coin V2**, **ESP32-S2**, **FreeRTOS**, and **ESP-IDF**.

The system continuously acquires physiological, activity, and environmental information at different rates, verifies whether sensor data is trustworthy, builds a synchronized health state, compares the user's current condition with a personal baseline, and performs Edge-AI risk estimation locally.

The safety layer then converts those risk estimates into controlled wellness alerts, emergency actions, or degraded operating modes.

The communication architecture uses **Wi-Fi for normal connectivity** and **GPS + LoRa for resilient emergency assistance** when conventional network connectivity is unavailable.


---

## Problem Statement

**Smart India Hackathon 2026 — Problem Statement #26181**

The project addresses the requirement for a secure personal health companion capable of continuous monitoring, early health-risk detection, privacy-preserving on-device intelligence, environmental awareness, disaster resilience, and emergency assistance.

HEALINK-SIH is designed particularly around situations in which health risks can increase because of:

- heat waves
- extreme environmental conditions
- disasters
- connectivity disruption
- delayed access to healthcare
- increased exposure or physical workload

The system is intended to provide **early risk awareness and actionable assistance**, rather than clinical diagnosis.

---

## What the program does

HEALINK reads physiological, motion, environmental, and contextual data from the wearable platform and processes the information through a fault-aware real-time pipeline.

The system follows:

```text
SENSE
   ↓
VERIFY
   ↓
TIME-ALIGN
   ↓
FUSE
   ↓
PERSONALIZE
   ↓
EDGE AI
   ↓
ESTIMATE RISK
   ↓
SAFETY DECISION
   ↓
WARN / ASSIST
