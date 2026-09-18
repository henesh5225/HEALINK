# HEALINK MPU6050-family motion driver

HEALINK uses `/dev/i2c1` at address `0x68` for the board labeled MPU-6050.

The driver reads `WHO_AM_I` from register `0x75` and accepts:

- `0x68` — MPU6050 identity
- `0x70` — MPU6500-family identity observed on the current breakout

The motion abstraction is intentionally kept device-neutral above the driver: X/Y/Z acceleration, vector magnitude, validity, and bounded fall-candidate state are exposed to HEALINK.

The acquisition path is 50 Hz, accelerometer range is +/-2 g, and the expected sensitivity is 16384 LSB/g for this configuration.

GPIO23 remains reserved for a future interrupt path. The current implementation uses deterministic polling so the physical motion data path can be validated independently before enabling hardware interrupts.
