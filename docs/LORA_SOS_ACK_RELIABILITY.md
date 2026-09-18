# HEALINK LoRa SOS/ACK Reliability Layer

## Physical topology

```text
QNX Raspberry Pi
  GPIO24 SOS button
        |
     EVENT_SOS
        |
  QNX event queue
        |
  emergency thread
        |
  dedicated LoRa worker
        |
     SX1278 #1
        )))) 433 MHz ((((
     SX1278 #2
        |
      ESP32
        |
  GPIO27 ACK button
```

The QNX terminal `S`/`s` key is a backup manual SOS trigger when the physical
GPIO24 button is unavailable. On the ESP32, `A`/`a` + Enter is the backup ACK
input when the physical GPIO27 button is unavailable.

## Reliability features

### 1. CRC

Both QNX and ESP32 enable the SX1278 payload CRC.

- QNX `RegModemConfig2 = 0x74` (SF7 + RxPayloadCrcOn)
- ESP32 uses `LoRa.enableCrc()`
- QNX rejects packets when `PAYLOAD_CRC_ERROR` is asserted

### 2. Bounded retries

QNX sends at most three SOS attempts.

```text
Attempt 1
   |
  ACK?
  /  \
YES   NO
 |     |
CONF   150 ms backoff
       |
    Attempt 2
       |
    Attempt 3
       |
  FAILURE
```

Each ACK wait is bounded to 2000 ms. The worker never retries forever.

### 3. Event-specific acknowledgement

Every SOS carries an `EVENT` identifier. The ESP32 returns the same identifier in `SOS_ACK`.

```text
HEALINK|SOS|NODE=QNX|EVENT=17|SOURCE=QNX
HEALINK|SOS_ACK|NODE=ESP32|EVENT=17|STATUS=ACKNOWLEDGED|RXRSSI=-42|RXSNR=9.50
```

### 4. RSSI / SNR

The ESP32 records the received SOS packet's RSSI/SNR and echoes those values in the ACK.

QNX independently measures the RSSI/SNR of the ACK it receives.

This provides two link-quality directions:

```text
QNX -> ESP32 : SOS RSSI/SNR
ESP32 -> QNX : ACK RSSI/SNR
```

### 5. Round-trip latency

QNX starts a monotonic timer immediately before each SOS transmission and records the receive timestamp when the matching ACK arrives.

The reported RTT includes:

```text
QNX TX -> RF -> ESP32 RX -> human ACK -> ESP32 TX -> RF -> QNX RX
```

This is intentionally an end-to-end application acknowledgement latency, not a radio-only propagation measurement.

### 6. Duplicate SOS handling

If QNX retries the same event because an ACK was lost, the ESP32 recognizes the previously acknowledged `EVENT` and repeats the ACK automatically. The physical ACK button is still required for the first acknowledgement.

## Expected QNX log

```text
[LORA] SOS TX attempt 1/3 | EVENT=17
[LORA] SOS TX complete | waiting for ACK (2000 ms)
[LORA] RX  ... HEALINK|SOS_ACK|EVENT=17|...
[LORA] ACK CONFIRMED | EVENT=17 | RTT=842 ms | ACK RSSI=-41 dBm | ACK SNR=9.50 dB | REMOTE SOS RSSI=-42 dBm | REMOTE SOS SNR=9.50 dB
[EMERGENCY] LORA ACK CONFIRMED | event=17 rtt=842 ms
```

## First validation order

1. Build the QNX project with the normal Momentics/QNX toolchain.
2. Flash the ESP32 remote firmware.
3. Verify both radios show 433 MHz / SF7 / BW125 / CR4/5 / CRC ON.
4. Press the physical QNX GPIO24 SOS button.
5. Confirm the ESP32 prints SOS RX and RSSI/SNR.
6. Press ESP32 GPIO27 ACK.
7. Confirm QNX prints the matching event ID, RTT, ACK RSSI/SNR and remote SOS RSSI/SNR.
8. Test retry by temporarily preventing the first ACK from being delivered.
9. Test duplicate recovery using the same event ID.

## Startup behavior

The event queue is cleared when HEALINK starts, so a stale `EVENT_SOS` from an earlier abnormal shutdown cannot trigger a new LoRa transmission automatically. The physical SOS input also establishes its startup level before edge detection, so a button already held LOW at process start does not generate an SOS.
