#include <SPI.h>
#include <LoRa.h>

/*
 * HEALINK remote LoRa node
 *
 * Node 1: QNX Raspberry Pi + SX1278 + physical SOS button
 * Node 2: ESP32 + SX1278 + physical operator ACK button
 *
 * ACK input methods:
 *   1) Primary: physical push button on GPIO27 -> GND
 *   2) Backup: Arduino Serial Monitor keyboard -> press A (or a) + Enter
 *
 * Reliability layer:
 *   - SX1278 hardware payload CRC enabled
 *   - event-specific ACK matching
 *   - duplicate SOS handling
 *   - remote RSSI/SNR echoed in the ACK
 */

#define LORA_SCK   18
#define LORA_MISO  19
#define LORA_MOSI  23
#define LORA_SS     5
#define LORA_RST   14
#define LORA_DIO0  26

#define ACK_BUTTON 27

#define LORA_FREQ 433E6

static bool sos_pending = false;
static unsigned long pending_event_id = 0UL;
static unsigned long last_acked_event_id = 0UL;
static long pending_sos_rssi = -999L;
static float pending_sos_snr = -999.0f;
static unsigned long last_button_change_ms = 0UL;
static const unsigned long BUTTON_DEBOUNCE_MS = 50UL;

static unsigned long parse_event_id(const String &msg)
{
  int pos = msg.indexOf("EVENT=");

  if (pos < 0) {
    return 0UL;
  }

  pos += 6;
  int end = msg.indexOf('|', pos);

  if (end < 0) {
    end = msg.length();
  }

  String value = msg.substring(pos, end);
  return strtoul(value.c_str(), nullptr, 10);
}

static void print_ack_instructions()
{
  Serial.println();
  Serial.println("***** ACK INPUT READY *****");
  Serial.println("1) Physical: press GPIO27 button");
  Serial.println("2) Backup  : type A in Serial Monitor and press Enter");
  Serial.println("****************************");
}

static bool send_ack(unsigned long event_id)
{
  Serial.println();
  Serial.println("----- HEALINK ACK TX -----");
  Serial.print("EVENT ID : ");
  Serial.println(event_id);

  LoRa.idle();
  const int result = LoRa.beginPacket();
  if (result == 0) {
    Serial.println("ACK TX  : BEGIN FAILED");
    LoRa.receive();
    return false;
  }

  LoRa.print("HEALINK|SOS_ACK|NODE=ESP32|EVENT=");
  LoRa.print(event_id);
  LoRa.print("|STATUS=ACKNOWLEDGED|RXRSSI=");
  LoRa.print(pending_sos_rssi);
  LoRa.print("|RXSNR=");
  LoRa.print(pending_sos_snr, 2);

  const int txResult = LoRa.endPacket();

  if (txResult == 0) {
    Serial.println("ACK TX  : FAILED");
    LoRa.receive();
    return false;
  }

  Serial.println("ACK TX  : SUCCESS");
  Serial.print("SOS RSSI: ");
  Serial.print(pending_sos_rssi);
  Serial.println(" dBm");
  Serial.print("SOS SNR : ");
  Serial.print(pending_sos_snr, 2);
  Serial.println(" dB");
  Serial.println("--------------------------");

  last_acked_event_id = event_id;
  sos_pending = false;
  pending_event_id = 0UL;
  pending_sos_rssi = -999L;
  pending_sos_snr = -999.0f;

  LoRa.receive();
  return true;
}

static void handle_serial_ack_input()
{
  while (Serial.available() > 0) {
    const int incoming = Serial.read();

    if (incoming == 'A' || incoming == 'a') {
      if (sos_pending) {
        Serial.println("BACKUP KEYBOARD ACK: A");
        (void)send_ack(pending_event_id);
      } else {
        Serial.println("No pending SOS; keyboard ACK ignored.");
      }
      continue;
    }

    if (incoming == 'H' || incoming == 'h' || incoming == '?') {
      Serial.println();
      Serial.println("HEALINK REMOTE COMMANDS");
      Serial.println("A = acknowledge pending SOS");
      Serial.println("H = show this help");
      Serial.println();
      continue;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  pinMode(ACK_BUTTON, INPUT_PULLUP);

  Serial.println();
  Serial.println("========================================");
  Serial.println("HEALINK ESP32 LoRa REMOTE NODE");
  Serial.println("========================================");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("ERROR: LoRa initialization failed!");
    while (true) {
      delay(1000);
    }
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setPreambleLength(8);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
  LoRa.receive();

  Serial.println("LoRa initialized successfully.");
  Serial.println("433 MHz / SF7 / BW125 / CR4/5 / SW0x12 / CRC ON");
  Serial.println("Physical ACK: GPIO27 -> push button -> GND");
  Serial.println("Backup ACK  : Serial Monitor -> A + Enter");
  Serial.println("Waiting for HEALINK SOS...");
}

void loop()
{
  /* Always service the backup keyboard input, even when no SOS is pending. */
  handle_serial_ack_input();

  const int packet_size = LoRa.parsePacket();

  if (packet_size > 0) {
    String msg;

    while (LoRa.available()) {
      msg += (char)LoRa.read();
    }

    const long rssi = LoRa.packetRssi();
    const float snr = LoRa.packetSnr();

    if (msg.startsWith("HEALINK|SOS|")) {
      const unsigned long event_id = parse_event_id(msg);

      if (event_id == 0UL) {
        Serial.println("[LORA] SOS ignored: missing EVENT ID.");
      } else if (event_id == last_acked_event_id) {
        Serial.println();
        Serial.println("[LORA] Duplicate ACKED SOS received; re-sending ACK.");
        Serial.print("EVENT ID: ");
        Serial.println(event_id);

        /*
         * QNX may retransmit because the earlier ACK was lost.
         * The operator already acknowledged this exact event, so
         * automatically repeat the ACK to recover the return path.
         */
        pending_sos_rssi = rssi;
        pending_sos_snr = snr;
        Serial.println("Duplicate of already acknowledged SOS; re-sending ACK.");
        (void)send_ack(event_id);
      } else if (sos_pending && event_id == pending_event_id) {
        /*
         * QNX may retransmit the same SOS while waiting for the operator ACK.
         * Do not print the full SOS banner again; keep the pending event and
         * refresh the most recent link metrics for the ACK.
         */
        pending_sos_rssi = rssi;
        pending_sos_snr = snr;
        Serial.print("[LORA] Duplicate SOS retry received | EVENT=");
        Serial.print(event_id);
        Serial.print(" | RSSI=");
        Serial.print(rssi);
        Serial.print(" dBm | SNR=");
        Serial.print(snr, 2);
        Serial.println(" dB");
      } else {
        sos_pending = true;
        pending_event_id = event_id;
        pending_sos_rssi = rssi;
        pending_sos_snr = snr;

        Serial.println();
        Serial.println("========================================");
        Serial.println("       !!! HEALINK SOS RECEIVED !!!");
        Serial.println("========================================");
        Serial.print("Message : ");
        Serial.println(msg);
        Serial.print("RSSI    : ");
        Serial.print(rssi);
        Serial.println(" dBm");
        Serial.print("SNR     : ");
        Serial.print(snr, 2);
        Serial.println(" dB");
        Serial.print("EVENT ID: ");
        Serial.println(pending_event_id);
        print_ack_instructions();
      }
    } else if (msg.startsWith("HEALINK|PING|")) {
      Serial.println("Legacy PING detected; sending compatibility ACK.");
      LoRa.idle();
      if (LoRa.beginPacket() != 0) {
        LoRa.print("HEALINK|ACK|NODE=ESP32|STATUS=RECEIVED");
        if (LoRa.endPacket() != 0) {
          Serial.println("Legacy ACK sent.");
        } else {
          Serial.println("Legacy ACK failed.");
        }
      } else {
        Serial.println("Legacy ACK begin failed.");
      }
      LoRa.receive();
    }

    LoRa.receive();
  }

  /* Primary physical ACK input. */
  if (sos_pending && digitalRead(ACK_BUTTON) == LOW) {
    const unsigned long now = millis();

    if (now - last_button_change_ms >= BUTTON_DEBOUNCE_MS) {
      last_button_change_ms = now;
      Serial.println("PHYSICAL ACK BUTTON PRESSED");
      (void)send_ack(pending_event_id);
    }
  }

  delay(5);
}
