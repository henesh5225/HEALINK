# HEALINK

QNX-based wearable health sensor-fusion project for QNX India eHACK 2026, Problem Statement #24.

## What the program does

HEALINK reads real hardware on a Raspberry Pi 4 running QNX. Independent sensor tasks acquire measurements at different rates and publish timestamped state into the fusion layer. The fusion pipeline evaluates freshness, signal quality and cross-sensor consistency before the classifier and deterministic safety logic produce the current health and safety state.

The design is intended to keep acquisition, fusion and safety processing separate while still allowing heterogeneous sensor streams to be combined at a common decision rate.

## Hardware

- MAX30102 - PPG, heart rate and SpO2
- AD8232 + ADS1115 - analog ECG acquisition path
- MPU6050 - motion and fall context
- DS18B20 - temperature
- DHT11 - ambient temperature and humidity context
- SSD1306 - local OLED interface
- SX1278 - LoRa emergency link

## Real-time sensor fusion

HEALINK uses multi-rate periodic workloads so each sensor can run at a rate appropriate to its data source while the fusion task operates on a synchronized view of the latest measurements.

```text
MAX30102   100 Hz   -> PPG / SpO2 / heart-rate information
AD8232     250 Hz   -> ECG acquisition / heart-rate information
MPU6050     50 Hz   -> motion / fall context
DS18B20      1 Hz   -> temperature
DHT11      0.5 Hz   -> ambient context
FUSION      20 Hz   -> synchronized health-state decision
```

Shared state carries the latest value, timestamp, validity and quality information. The fusion layer checks whether each input is fresh enough to be trusted and whether independent measurements agree before using them as evidence for classification.

## Fault-aware operation

HEALINK explicitly distinguishes between different sensor conditions instead of treating every missing or abnormal sample as valid data.

```text
VALID          sensor data is available and current
STALE          expected updates stopped or exceeded the freshness limit
OFFLINE        device is unavailable or could not be brought online
FAULT          driver or acquisition path reported a failure
INCONSISTENT   independent measurements disagree beyond the configured policy
```

These conditions are represented in the runtime state masks and are visible through the CLI. The fusion and safety layers use the resulting state to reduce or reject unreliable evidence, preserve the last known state only where appropriate, and recognize recovery when valid updates resume.

## Runtime monitoring and performance evidence

The main `healink` CLI provides application-level observability for health state, sensor state and periodic timing behavior. It reports the validity/freshness/fault masks together with per-workload timing information such as configured period, average execution time, maximum execution time, start jitter, deadline misses and schedule skips.

A typical performance evidence flow is:

```text
./healink
   |
   +-- sensor / fusion state
   +-- per-task timing statistics
   +-- fusion latency information
   +-- deadline / schedule exception counters
   |
   +-- QNX System Profiler (independent CPU/thread evidence)
```

The CLI is used for application-specific measurements; the QNX System Profiler is used to inspect CPU usage and thread scheduling behavior. Final benchmark values are taken from the target Raspberry Pi run rather than from illustrative documentation examples.

## Reliable emergency communication

The SX1278 link is treated as a reliability-oriented event channel rather than a simple text transmitter. Events carry identifiers and integrity information, while the communication path records retry attempts, RSSI/SNR and acknowledgment timing. The ESP32 remote node provides the acknowledgment endpoint.

This communication path is kept separate from the sensor-fusion decision path so that local health classification remains available even when the radio link is unavailable.

## Build

From a QNX-enabled Momentics shell:

```text
make clean
make -j12 all
```

Useful checks:

```text
make test
make diag
make lora-tools
```

For the target-side lifecycle check:

```text
sh tools/qnx_a10_soak.sh 3 60
```

## Source layout

```text
include/        shared headers
src/core/       state, IPC, fusion, classifier, safety
src/sensors/    sensor drivers
src/platform/   QNX timing, I2C and Raspberry Pi GPIO
src/comms/      SX1278/LoRa worker
src/ui/         SSD1306 driver and HEALINK UI
src/diag/       unified I2C diagnostic
tests/          maintained fusion and safety tests
tools/          small bring-up and lifecycle tools
esp32/          remote LoRa ACK firmware
docs/           current project notes
bsp_config/     QNX SPI configuration
scripts/        BSP staging helper
```

Generated build products are intentionally not stored in this source package.
