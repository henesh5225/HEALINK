# DS18B20 — QNX 8 / Raspberry Pi 4

The HEALINK DS18B20 backend uses the BCM2711 GPIO block directly because a 1-Wire slot needs microsecond-level direction changes and sampling. GPIO4 is used as the data line.

## Software

The implementation uses the QNX APIs exposed by the normal gnu17/qcc environment:

- `nanospin_ns()` for sub-millisecond 1-Wire timing
- `nanosleep()` for the long DS18B20 temperature-conversion wait
- `mmap_device_memory()` for the BCM2711 GPIO register mapping
- `ThreadCtl(_NTO_TCTL_IO_PRIV, 0)` before direct hardware access

The source deliberately does not define `_POSIX_C_SOURCE` because doing so can hide QNX-specific declarations and constants such as `nanospin_ns()` and `PROT_NOCACHE` from the headers. QNX documents `_QNX_SOURCE` as the feature set containing everything in the headers.

## Hardware

Use a 3.3 V DS18B20 with a 4.7 kOhm pull-up from DATA to 3.3 V. Connect DATA to Raspberry Pi BCM GPIO4. The bus is operated as open-drain style: HEALINK actively drives LOW and changes the pin to INPUT to release the line HIGH.

## Runtime permission

`mmap_device_memory()` requires the process to have the physical-memory ability (`PROCMGR_AID_MEM_PHYS`) on the target. `ThreadCtl(_NTO_TCTL_IO_PRIV, 0)` alone is not a guarantee that the physical mapping will be permitted. If the build succeeds but initialization reports a mapping/permission error on the target, check the QNX process ability policy for the HEALINK binary.

## Protocol

Initialization performs a real 1-Wire reset/presence check. Reads perform:

1. Reset + presence
2. `SKIP ROM` (`0xCC`)
3. `CONVERT T` (`0x44`)
4. Wait for up to 750 ms for 12-bit conversion
5. Reset + `SKIP ROM` + `READ SCRATCHPAD` (`0xBE`)
6. Read all 9 scratchpad bytes
7. CRC8 verification
8. Signed 16-bit raw temperature conversion at 1/16 °C resolution

No synthetic temperature is generated. A missing device remains offline, and a communication/CRC/validity failure is returned to the existing HEALINK sensor-state logic.
