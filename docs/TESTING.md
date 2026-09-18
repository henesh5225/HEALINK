# HEALINK Testing

The final repository keeps one deterministic software test suite, one unified
I2C diagnostic, two SX1278 bring-up tools, and one QNX lifecycle soak script.

## Deterministic unit/state tests

```sh
make test
```

The test fixture targets the current production sensor model, including MPU6050
motion state and the current fusion/classifier/staleness/safety behavior.

## Unified I2C diagnostic

```sh
make diag
sudo ./healink_diag
```

This probes the core I2C devices used by the runtime.

## LoRa SPI bring-up

```sh
make lora-tools
sudo ./sx1278_probe
sudo ./sx1278_repeat
```

## A10 lifecycle validation

On the QNX Raspberry Pi after building both the application and test target:

```sh
sh tools/qnx_a10_soak.sh 3 60
```

The soak script checks deterministic tests, startup, clean shutdown, resource
integrity, and timing failure markers without generating synthetic sensor data.

## Final hardware evidence

The final demo evidence should separately record:

- QNX AArch64 clean build.
- Core sensor availability and runtime data quality.
- LoRa `RegVersion=0x12` and repeated SPI proof.
- Physical SOS -> ESP32 reception -> physical/keyboard ACK -> QNX confirmation.
- OLED event presentation.
