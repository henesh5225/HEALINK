# DHT11 on QNX Raspberry Pi 4

HEALINK includes DHT11 as an environmental context sensor on BCM GPIO22.

## Wiring

| DHT11 | Raspberry Pi 4 |
|---|---|
| VCC | 3.3 V |
| DATA | BCM GPIO22 (physical pin 15) |
| GND | GND |

Use a pull-up from DATA to 3.3 V when the breakout does not already provide one. Do not drive the Pi data line from 5 V.

## Runtime model

- GPIO22 is bit-banged because DHT11 is a single-wire timing protocol.
- Direct BCM2711 GPIO register access is used by the DHT11 driver.
- Each read measures all 40 data bits using `ClockCycles()` and checks the checksum.
- HEALINK samples DHT11 every 2 seconds.
- DHT11 contributes `ambient_temperature_c` and `humidity_pct`. It does not overwrite the DS18B20 `temperature_c` field.
- `SENSOR_DHT11` is a separate data-quality bit and is intentionally excluded from `SENSOR_ALL_HEALTH`, so environmental data cannot create physiological confidence.

## Runtime validation

DHT11 status is reported by the production `./healink` runtime. There is no
separate DHT11-only diagnostic in the final source tree.

The production runtime reports initialization/read status and keeps DHT11 in
its own data-quality bit.
