# HEALINK

QNX-based wearable health sensor-fusion project for QNX India eHACK 2026, Problem Statement #24.

## What the program does

HEALINK reads real hardware on a Raspberry Pi 4 running QNX. Sensor threads run at different rates and update shared state. The fusion layer checks freshness, signal quality and agreement between measurements. The classifier and deterministic safety layer then turn that information into health and safety states.

The project does not create fake sensor values. A missing device is shown as OFFLINE, and a missed update can become STALE.

## Hardware

- MAX30102 - PPG, heart rate and SpO2
- AD8232 + ADS1115 - analog ECG acquisition path
- MPU6050 - motion and fall context
- DS18B20 - temperature
- DHT11 - ambient temperature and humidity context
- SSD1306 - local OLED interface
- SX1278 - LoRa emergency link

## Manual SOS test

The QNX-side LoRa test can be triggered in either of these ways:

```text
GPIO24 button       -> EVENT_SOS
Keyboard S          -> EVENT_SOS
```

The final build is manual-only for LoRa. Sensor distress, fall, and emergency events are still evaluated and logged, but they do not start LoRa transmission.

The ESP32 remote node accepts an ACK from:

```text
GPIO27 button
Serial Monitor: A + Enter
```

The LoRa path uses CRC, event IDs, bounded retries, RSSI/SNR and ACK timing.

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
tools/          small bring-up and soak tools
esp32/          remote LoRa ACK firmware
docs/           current project notes
bsp_config/     QNX SPI configuration
scripts/        BSP staging helper
```

Generated build products are intentionally not stored in this source package.

## LoRa trigger

LoRa SOS transmission is manual-only in this final build. It is generated only by the QNX GPIO24 SOS button or the QNX keyboard `S` backup. Sensor distress, fall, and emergency events are logged and handled locally but never start a LoRa transmission.
