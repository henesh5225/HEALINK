# HEALINK 3.0 hardware plan — Raspberry Pi 4

## Core bus map

| Device | Bus | Address / Pin | Purpose |
|---|---|---:|---|
| MAX30102 | I2C1 | 0x57 | PPG HR + SpO2 |
| MPU6050 | I2C1 | 0x68 | Motion + fall |
| SSD1306 | I2C1 | 0x3C | Live HUD |
| ADS1115 | I2C1 | 0x48 | ADC for AD8232 ECG output |
| AD8232 | Analog | ADS1115 A0 | ECG waveform / HR |
| DS18B20 | GPIO | BCM GPIO4 | Body/skin temperature |
| DHT11 | GPIO | BCM GPIO22 | Ambient temp/humidity (SHOULD) |
| Rain sensor | GPIO | BCM GPIO17 | Rain context (SHOULD) |

Raspberry Pi I2C1 normally uses BCM GPIO2 (SDA) and GPIO3 (SCL).

## Important electrical correction


## Power

Use the module's documented supply requirements. Keep all sensor grounds common with the Raspberry Pi. Do not connect a sensor output above the Raspberry Pi's supported GPIO voltage.

## MPU6050 interrupt

Reserve BCM GPIO23 for the MPU6050 INT line in the wiring plan. The actual QNX interrupt/vector mapping must be verified against the Raspberry Pi 4 BSP before enabling the interrupt-driven Emergency_Manager. Do not assume that GPIO number 23 is the QNX IRQ number.

## QNX GPIO direction backend

On the Raspberry Pi 4 BCM2711 QNX BSP, `/dev/gpio/<n>` is used for GPIO level I/O, while the BSP `gpio-bcm2711` utility is used by HEALINK at initialization time to configure input/output direction. The direction command is deliberately kept out of the hard real-time sensor loops.

AD8232 lead-off GPIO numbers are intentionally left unassigned in `include/healink_config.h` until the exact breakout-board wiring is selected. HEALINK therefore reports lead-off status as unavailable rather than guessing a pin mapping.

## Physical LoRa SOS button

The QNX-side LoRa #1 uses BCM GPIO24 as a dedicated active-low physical SOS button.

| Signal | BCM GPIO | Raspberry Pi physical pin | Connection |
|---|---:|---:|---|
| SOS button | 24 | 18 | Button to GND |
| GND | — | 20 | Common ground |
| 3.3 V pull-up | — | 17 | 10 kOhm resistor to GPIO24 |

The HEALINK GPIO backend configures GPIO24 as an input. The application samples it at 10 ms
and accepts a new state only after 5 consistent samples (50 ms debounce). No keyboard SOS shortcut
is used. The `D` keyboard shortcut remains only for replaying the cinematic OLED demo.

## ESP32 LoRa #2 ACK button

The ESP32 remote node uses GPIO27 as an active-low ACK button with the ESP32 internal pull-up:

```text
ESP32 GPIO27 ---- push button ---- GND
```

## LoRa reliability layer

The production SOS/ACK path uses the same SX1278 settings on both ends:

```text
433 MHz
SF7
BW 125 kHz
CR 4/5
Sync word 0x12
Payload CRC: ON
```

QNX retries an SOS up to 3 times, with a 150 ms backoff between attempts, and waits up to 2000 ms for a matching ACK on each attempt. QNX reports the ACK RSSI/SNR and end-to-end acknowledgement latency. The ESP32 records the received SOS RSSI/SNR and includes those values in its ACK packet.
