# HEALINK target tools

The final tree intentionally keeps only tools that are still useful for final
bring-up or repeatable validation:

- `sx1278_probe.c` — single SX1278 identity probe (`RegVersion=0x12`).
- `sx1278_repeat.c` — repeated SPI read test used to validate stable QNX SPI
  configuration and transfer behavior.
- `qnx_a10_soak.sh` — target-side lifecycle/robustness harness.
