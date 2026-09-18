# HEALINK I2C diagnostic

`healink_diag` is a small target-side bring-up tool. It validates the QNX I2C
resource-manager path before the multi-threaded HEALINK application starts.

It checks:

- MAX30102 part ID at `0x57`, register `0xFF` (expected `0x15`)
- MPU6050-family `WHO_AM_I` at `0x68`, register `0x75` (accept `0x68` for MPU6050 or `0x70` for the MPU6500-family silicon observed on the current breakout)
- ADS1115 configuration register at `0x48`, register `0x01`
- SSD1306 acknowledgement at `0x3C` using its no-op command; this display has
  no identity register, so acknowledgement alone is not a display test.

## Build

From a QNX SDP environment in the project root:

```sh
make clean
make diag
```

## Run on the Raspberry Pi QNX target

First confirm the I2C resource manager exposes the configured bus:

```sh
ls /dev/i2c*
```

Copy `healink_diag` to the target and run:

```sh
./healink_diag
```

Do not start the full `healink` application until expected devices respond.
