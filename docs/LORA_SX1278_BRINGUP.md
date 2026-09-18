# HEALINK SX1278 / LoRa

HEALINK uses raw LoRa point-to-point communication between the QNX Raspberry
Pi and an ESP32 remote acknowledgement node.

## QNX LoRa #1

- SPI device: `/dev/io-spi/spi0/dev0`
- VCC: Raspberry Pi 3.3 V
- GND: common ground
- SCK: GPIO11 / physical pin 23
- MOSI: GPIO10 / physical pin 19
- MISO: GPIO9 / physical pin 21
- NSS/CS: GPIO8 / physical pin 24
- SOS button: BCM GPIO24 / physical pin 18 to GND, external 10 kOhm pull-up to 3.3 V

## ESP32 LoRa #2

- SCK GPIO18
- MISO GPIO19
- MOSI GPIO23
- NSS/CS GPIO5
- RESET GPIO14
- DIO0 GPIO26
- ACK button GPIO27 to GND using the ESP32 internal pull-up

## RF settings

Both nodes use:

```text
433 MHz
SF7
BW 125 kHz
CR 4/5
Sync word 0x12
Payload CRC ON
```

The QNX source-of-truth SPI configuration is `bsp_config/spi/spi.conf`.

## Bring-up tools

Only two standalone LoRa tools are retained in the final tree:

```sh
sudo ./sx1278_probe
sudo ./sx1278_repeat
```

`RegVersion` must return `0x12`. The repeat test should produce 10/10 valid
reads before RF testing.

## Integrated SOS/ACK path

```text
GPIO24 / keyboard S
        |
     EVENT_SOS
        |
   QNX event queue
        |
  emergency thread
        |
    LoRa worker
        |
      SX1278
        )))) 433 MHz (((
      SX1278
        |
      ESP32
        |
 GPIO27 / keyboard A
        |
    SOS_ACK
```

QNX allows three bounded SOS attempts, with a 150 ms retry backoff and a
2000 ms ACK wait per attempt. ACKs are matched to the same event ID and link
metrics are recorded.

## Manual test mode

The final build is manual-only. Only GPIO24 or
QNX keyboard `S` starts LoRa SOS transmission. Sensor emergency events remain
visible but do not start LoRa transmission. Change the setting to `0` only
after the physical sensor-triggered emergency path is ready for validation.
