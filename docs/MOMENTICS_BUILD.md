# Momentics build instructions

## 1. Create the project

In QNX Momentics:

File -> New -> QNX C/C++ Project -> QNX Executable

Project name:

`HEALINK_3.0`

Target architecture:

`aarch64le`

Toolchain/variant:

`gcc_ntoaarch64le`

Language:

C

Create the project, then replace the generated source tree with the contents of this package.

## 2. Project tree

Import the supplied project tree as-is. The maintained source directories are:

- `include`
- `src/core`
- `src/comms`
- `src/platform`
- `src/sensors`
- `src/ui`
- `src/diag`
- `tests`
- `tools`

Make sure `include` is in the compiler include search path. The project Makefile
controls the maintained build targets.

## 3. Build

Use Project -> Build Project.

Command-line equivalent from a QNX-environment shell:

```sh
make clean
make -j12 all
```

The executable is `healink`. Additional maintained targets are:

```sh
make test
make diag
make lora-tools
```

## 4. What should build now

This code is intentionally real-hardware-only. It does not generate fake sensor values. With no target board/hardware, the build is still valid, but the application cannot produce live sensor readings.

The QNX I2C layer uses `devctl()` with the QNX 8 I2C framework. The GPIO path assumes the Raspberry Pi 4 QNX image's `rpi_gpio` resource manager is present.

## 5. When the Raspberry Pi arrives

First verify the target services:

```sh
pidin | grep -E "rpi_gpio|i2c-bcm2711|spi-bcm2711"
ls /dev/i2c*
ls /dev/gpio
```

Then connect only the I2C devices and test in this order:

1. SSD1306 @ 0x3C
2. MAX30102 @ 0x57
3. MPU6050 @ 0x68
4. ADS1115 @ 0x48 + AD8232 on AIN0

Do not connect LoRa/GPS until the health core is stable.

## 6. Expected behavior with missing hardware

Example:

```text
[HEALINK] Initializing QNX hardware interfaces...
[HEALINK] MAX30102  OFFLINE (hardware not present or backend unavailable)
[HEALINK] ADS1115   OFFLINE (hardware not present or backend unavailable)
[HEALINK] AD8232    OFFLINE (hardware not present or backend unavailable)
[HEALINK] MPU6050   OFFLINE (hardware not present or backend unavailable)
[HEALINK] SSD1306   OFFLINE (hardware not present or backend unavailable)
[HEALINK] DS18B20   OFFLINE (hardware not present or backend unavailable)

[HEALINK] CORE STARTED
[HEALINK] No simulated data is generated. Missing hardware stays OFFLINE/STALE.
```
