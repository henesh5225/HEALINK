#define _POSIX_C_SOURCE 200809L

#include "healink_lora.h"

#include <devctl.h>
#include <errno.h>
#include <fcntl.h>
#include <hw/io-spi.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "healink_config.h"
#include "healink_ipc.h"
#include "healink_types.h"

/* The SPI transfer format matches the working QNX SX1278 bring-up. */

#define LORA_SPI_DEV "/dev/io-spi/spi0/dev0"

#define REG_FIFO                 0x00u
#define REG_OP_MODE              0x01u
#define REG_FRF_MSB              0x06u
#define REG_FIFO_ADDR_PTR        0x0Du
#define REG_FIFO_RX_CURRENT_ADDR 0x10u
#define REG_IRQ_FLAGS            0x12u
#define REG_RX_NB_BYTES          0x13u
#define REG_MODEM_CONFIG1        0x1Du
#define REG_MODEM_CONFIG2        0x1Eu
#define REG_PKT_RSSI_VALUE       0x1Au
#define REG_PKT_SNR_VALUE        0x1Bu
#define REG_PREAMBLE_MSB         0x20u
#define REG_MODEM_CONFIG3        0x26u
#define REG_SYNC_WORD            0x39u
#define REG_VERSION              0x42u

#define OP_MODE_LONG_RANGE 0x80u
#define OP_MODE_SLEEP      0x00u
#define OP_MODE_STDBY      0x01u
#define OP_MODE_TX         0x03u
#define OP_MODE_RX_CONT    0x05u

#define IRQ_RX_DONE           0x40u
#define IRQ_TX_DONE           0x08u
#define IRQ_PAYLOAD_CRC_ERROR 0x20u
#define IRQ_CLEAR_ALL         0xFFu

#define EXPECTED_VERSION 0x12u
#define FRF_433_MHZ      0x6C4000u

#define ACK_TIMEOUT_MS    2000UL
#define MAX_RETRIES       3U
#define RETRY_BACKOFF_MS  150UL
#define RADIO_POLL_MS     5UL

#define LORA_CMD_QUEUE "/healink_lora_cmd"
#define LORA_CMD_DEPTH 8L
#define LORA_MAX_PACKET 120U

typedef enum {
    LORA_CMD_NONE = 0,
    LORA_CMD_SOS = 1
} healink_lora_cmd_type_t;

typedef struct {
    uint32_t type;
    uint32_t event_id;
    uint64_t timestamp_ns;
    uint32_t source_event_type;
} healink_lora_cmd_t;

static void sleep_ms(unsigned long ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000UL);
    ts.tv_nsec = (long)((ms % 1000UL) * 1000000UL);
    (void)nanosleep(&ts, NULL);
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }

    return (uint64_t)ts.tv_sec * 1000ULL +
           (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int spi_xfer(
    int spi_fd,
    const uint8_t *tx,
    uint8_t *rx,
    size_t nbytes)
{
    if (tx == NULL || nbytes == 0U || (nbytes % 4U) != 0U) {
        errno = EINVAL;
        return -1;
    }

    const size_t bytes = sizeof(spi_xchng_t) + nbytes;
    spi_xchng_t *msg = (spi_xchng_t *)calloc(1U, bytes);

    if (msg == NULL) {
        return -1;
    }

    msg->nbytes = (uint32_t)nbytes;
    memcpy(msg->data, tx, nbytes);

    const int status =
        devctl(spi_fd, DCMD_SPI_DATA_XCHNG, msg, bytes, NULL);

    if (status != EOK) {
        free(msg);
        errno = status;
        return -1;
    }

    if (rx != NULL) {
        memcpy(rx, msg->data, nbytes);
    }

    free(msg);
    return 0;
}

static int sx_read(
    int spi_fd,
    uint8_t reg,
    uint8_t *value)
{
    const uint8_t tx[4] = {
        (uint8_t)(reg & 0x7Fu), 0U, 0U, 0U
    };
    uint8_t rx[4] = {0U, 0U, 0U, 0U};

    if (spi_xfer(spi_fd, tx, rx, sizeof(tx)) != 0) {
        return -1;
    }

    if (value != NULL) {
        *value = rx[1];
    }

    return 0;
}

static int sx_write3(
    int spi_fd,
    uint8_t start,
    uint8_t a,
    uint8_t b,
    uint8_t c)
{
    const uint8_t tx[4] = {
        (uint8_t)(start | 0x80u), a, b, c
    };
    uint8_t rx[4] = {0U, 0U, 0U, 0U};

    return spi_xfer(spi_fd, tx, rx, sizeof(tx));
}

static int sx_write_fifo(
    int spi_fd,
    const uint8_t *data,
    size_t len)
{
    if (data == NULL || len == 0U || len > LORA_MAX_PACKET) {
        errno = EINVAL;
        return -1;
    }

    const size_t padded =
        ((1U + len) + 3U) & ~3U;

    uint8_t *tx = (uint8_t *)calloc(1U, padded);
    uint8_t *rx = (uint8_t *)calloc(1U, padded);

    if (tx == NULL || rx == NULL) {
        free(tx);
        free(rx);
        return -1;
    }

    tx[0] = (uint8_t)(REG_FIFO | 0x80u);
    memcpy(&tx[1], data, len);

    const int result =
        spi_xfer(spi_fd, tx, rx, padded);

    free(tx);
    free(rx);

    return result;
}

static int sx_read_fifo(
    int spi_fd,
    uint8_t *data,
    size_t len)
{
    if (data == NULL || len == 0U || len > LORA_MAX_PACKET) {
        errno = EINVAL;
        return -1;
    }

    const size_t padded =
        ((1U + len) + 3U) & ~3U;

    uint8_t *tx = (uint8_t *)calloc(1U, padded);
    uint8_t *rx = (uint8_t *)calloc(1U, padded);

    if (tx == NULL || rx == NULL) {
        free(tx);
        free(rx);
        return -1;
    }

    tx[0] = REG_FIFO;
    const int result =
        spi_xfer(spi_fd, tx, rx, padded);

    if (result == 0) {
        memcpy(data, &rx[1], len);
    }

    free(tx);
    free(rx);

    return result;
}

static int configure_radio(int spi_fd)
{
    if (sx_write3(
            spi_fd,
            REG_OP_MODE,
            OP_MODE_LONG_RANGE | OP_MODE_SLEEP,
            0U,
            0U) != 0) {
        return -1;
    }

    sleep_ms(10UL);

    if (sx_write3(
            spi_fd,
            REG_OP_MODE,
            OP_MODE_LONG_RANGE | OP_MODE_STDBY,
            0U,
            0U) != 0) {
        return -1;
    }

    sleep_ms(5UL);

    if (sx_write3(
            spi_fd,
            REG_FRF_MSB,
            (uint8_t)((FRF_433_MHZ >> 16) & 0xFFu),
            (uint8_t)((FRF_433_MHZ >> 8) & 0xFFu),
            (uint8_t)(FRF_433_MHZ & 0xFFu)) != 0) {
        return -1;
    }

    /*
     * Complete the word beginning at 0x07. This preserves the already
     * proven PA configuration used by the QNX/ESP32 RF test.
     */
    if (sx_write3(
            spi_fd,
            0x07u,
            (uint8_t)((FRF_433_MHZ >> 8) & 0xFFu),
            (uint8_t)(FRF_433_MHZ & 0xFFu),
            0x88u) != 0) {
        return -1;
    }

    if (sx_write3(
            spi_fd,
            REG_FIFO_ADDR_PTR,
            0x00u,
            0x80u,
            0x00u) != 0) {
        return -1;
    }

    /* SF7 / BW125 / CR4/5 / explicit header. */
    if (sx_write3(
            spi_fd,
            REG_MODEM_CONFIG1,
            0x72u,
            0x70u,
            0x00u) != 0) {
        return -1;
    }

    /* SF7 + RxPayloadCrcOn. 0x74 = SF7 | payload CRC enabled. */
    if (sx_write3(
            spi_fd,
            REG_MODEM_CONFIG2,
            0x74u,
            0x00u,
            0x00u) != 0) {
        return -1;
    }

    if (sx_write3(
            spi_fd,
            REG_PREAMBLE_MSB,
            0x00u,
            0x08u,
            0x00u) != 0) {
        return -1;
    }

    if (sx_write3(
            spi_fd,
            0x23u,
            0xFFu,
            0x00u,
            0x00u) != 0) {
        return -1;
    }

    if (sx_write3(
            spi_fd,
            REG_MODEM_CONFIG3,
            0x04u,
            0x00u,
            0x00u) != 0) {
        return -1;
    }

    if (sx_write3(
            spi_fd,
            REG_SYNC_WORD,
            0x12u,
            0x20u,
            0x1Du) != 0) {
        return -1;
    }

    return 0;
}

static int radio_tx(
    int spi_fd,
    const char *payload)
{
    const size_t len = strlen(payload);

    if (configure_radio(spi_fd) != 0 ||
        sx_write3(
            spi_fd,
            REG_FIFO_ADDR_PTR,
            0x80u,
            0x80u,
            0x00u) != 0 ||
        sx_write_fifo(
            spi_fd,
            (const uint8_t *)payload,
            len) != 0 ||
        sx_write3(
            spi_fd,
            0x22u,
            (uint8_t)len,
            0x00u,
            0x00u) != 0 ||
        sx_write3(
            spi_fd,
            REG_IRQ_FLAGS,
            IRQ_CLEAR_ALL,
            0U,
            0U) != 0 ||
        sx_write3(
            spi_fd,
            REG_OP_MODE,
            OP_MODE_LONG_RANGE | OP_MODE_TX,
            0U,
            0U) != 0) {
        return -1;
    }

    const uint64_t deadline =
        now_ms() + 1500ULL;

    while (now_ms() <= deadline) {
        uint8_t irq = 0U;

        if (sx_read(spi_fd, REG_IRQ_FLAGS, &irq) != 0) {
            return -1;
        }

        if ((irq & IRQ_TX_DONE) != 0U) {
            (void)sx_write3(
                spi_fd,
                REG_IRQ_FLAGS,
                IRQ_TX_DONE,
                0U,
                0U);

            (void)sx_write3(
                spi_fd,
                REG_OP_MODE,
                OP_MODE_LONG_RANGE | OP_MODE_STDBY,
                0U,
                0U);

            return 0;
        }

        sleep_ms(RADIO_POLL_MS);
    }

    (void)sx_write3(
        spi_fd,
        REG_OP_MODE,
        OP_MODE_LONG_RANGE | OP_MODE_STDBY,
        0U,
        0U);

    return -1;
}

typedef struct {
    int rssi_dbm;
    float snr_db;
    float remote_sos_rssi_dbm;
    float remote_sos_snr_db;
    uint64_t rx_ts_ms;
} lora_ack_info_t;

static int radio_read_link_metrics(
    int spi_fd,
    int *rssi_dbm,
    float *snr_db)
{
    uint8_t raw_rssi = 0U;
    uint8_t raw_snr = 0U;

    if (sx_read(spi_fd, REG_PKT_RSSI_VALUE, &raw_rssi) != 0 ||
        sx_read(spi_fd, REG_PKT_SNR_VALUE, &raw_snr) != 0) {
        return -1;
    }

    /* 433 MHz is in the SX1278 low-frequency range. */
    if (rssi_dbm != NULL) {
        *rssi_dbm = (int)raw_rssi - 164;
    }

    if (snr_db != NULL) {
        *snr_db = (float)(int8_t)raw_snr * 0.25f;
    }

    return 0;
}

static int parse_float_field(
    const char *packet,
    const char *key,
    float *value)
{
    const char *text = strstr(packet, key);

    if (text == NULL || value == NULL) {
        return -1;
    }

    text += strlen(key);

    char *endptr = NULL;
    float parsed = strtof(text, &endptr);

    if (endptr == text) {
        return -1;
    }

    *value = parsed;
    return 0;
}

static int radio_receive_ack(
    int spi_fd,
    uint32_t expected_event_id,
    unsigned long timeout_ms,
    lora_ack_info_t *info)
{
    char packet[LORA_MAX_PACKET + 1U];

    if (info != NULL) {
        memset(info, 0, sizeof(*info));
        info->rssi_dbm = -999;
        info->remote_sos_rssi_dbm = -999.0f;
        info->remote_sos_snr_db = -999.0f;
    }

    if (configure_radio(spi_fd) != 0 ||
        sx_write3(
            spi_fd,
            REG_FIFO_ADDR_PTR,
            0x00u,
            0x80u,
            0x00u) != 0 ||
        sx_write3(
            spi_fd,
            REG_IRQ_FLAGS,
            IRQ_CLEAR_ALL,
            0U,
            0U) != 0 ||
        sx_write3(
            spi_fd,
            REG_OP_MODE,
            OP_MODE_LONG_RANGE | OP_MODE_RX_CONT,
            0U,
            0U) != 0) {
        return -1;
    }

    const uint64_t deadline =
        now_ms() + (uint64_t)timeout_ms;

    while (now_ms() <= deadline) {
        uint8_t irq = 0U;

        if (sx_read(spi_fd, REG_IRQ_FLAGS, &irq) != 0) {
            return -1;
        }

        if ((irq & IRQ_RX_DONE) != 0U) {
            if ((irq & IRQ_PAYLOAD_CRC_ERROR) != 0U) {
                printf("[LORA] RX packet rejected by CRC\n");
                (void)sx_write3(
                    spi_fd,
                    REG_IRQ_FLAGS,
                    IRQ_RX_DONE | IRQ_PAYLOAD_CRC_ERROR,
                    0U,
                    0U);
                continue;
            }

            uint8_t count = 0U;
            uint8_t current = 0U;

            if (sx_read(spi_fd, REG_RX_NB_BYTES, &count) != 0 ||
                sx_read(spi_fd, REG_FIFO_RX_CURRENT_ADDR, &current) != 0) {
                return -1;
            }

            if (count == 0U) {
                (void)sx_write3(
                    spi_fd,
                    REG_IRQ_FLAGS,
                    IRQ_RX_DONE,
                    0U,
                    0U);
                continue;
            }

            const size_t n =
                ((size_t)count < LORA_MAX_PACKET)
                    ? (size_t)count
                    : LORA_MAX_PACKET - 1U;

            if (sx_write3(
                    spi_fd,
                    REG_FIFO_ADDR_PTR,
                    current,
                    0x80u,
                    0x00u) != 0 ||
                sx_read_fifo(
                    spi_fd,
                    (uint8_t *)packet,
                    n) != 0) {
                return -1;
            }

            packet[n] = '\0';

            int packet_rssi_dbm = -999;
            float packet_snr_db = -999.0f;

            if (radio_read_link_metrics(
                    spi_fd,
                    &packet_rssi_dbm,
                    &packet_snr_db) != 0) {
                printf("[LORA] Could not read RSSI/SNR for received packet\n");
            }

            if (info != NULL) {
                info->rssi_dbm = packet_rssi_dbm;
                info->snr_db = packet_snr_db;
                info->rx_ts_ms = now_ms();
            }

            printf(
                "[LORA] RX %u bytes | %s\n",
                (unsigned)n,
                packet);

            (void)sx_write3(
                spi_fd,
                REG_IRQ_FLAGS,
                IRQ_RX_DONE,
                0U,
                0U);

            (void)sx_write3(
                spi_fd,
                REG_OP_MODE,
                OP_MODE_LONG_RANGE | OP_MODE_STDBY,
                0U,
                0U);

            const char *prefix =
                "HEALINK|SOS_ACK|";

            if (strncmp(
                    packet,
                    prefix,
                    strlen(prefix)) != 0) {
                printf("[LORA] Non-ACK packet ignored while waiting for EVENT=%lu\n",
                       (unsigned long)expected_event_id);

                (void)sx_write3(
                    spi_fd,
                    REG_IRQ_FLAGS,
                    IRQ_RX_DONE,
                    0U,
                    0U);

                (void)sx_write3(
                    spi_fd,
                    REG_OP_MODE,
                    OP_MODE_LONG_RANGE | OP_MODE_RX_CONT,
                    0U,
                    0U);

                continue;
            }

            const char *event_text =
                strstr(packet, "EVENT=");

            if (event_text == NULL) {
                return 0;
            }

            event_text += strlen("EVENT=");

            char *endptr = NULL;
            unsigned long received =
                strtoul(event_text, &endptr, 10);

            if (endptr == event_text ||
                received > 0xFFFFFFFFUL) {
                printf("[LORA] ACK ignored | malformed EVENT field\n");
                (void)sx_write3(
                    spi_fd,
                    REG_OP_MODE,
                    OP_MODE_LONG_RANGE | OP_MODE_RX_CONT,
                    0U,
                    0U);
                continue;
            }

            if ((uint32_t)received != expected_event_id) {
                printf(
                    "[LORA] ACK ignored | expected=%lu received=%lu\n",
                    (unsigned long)expected_event_id,
                    received);

                (void)sx_write3(
                    spi_fd,
                    REG_IRQ_FLAGS,
                    IRQ_RX_DONE,
                    0U,
                    0U);

                (void)sx_write3(
                    spi_fd,
                    REG_OP_MODE,
                    OP_MODE_LONG_RANGE | OP_MODE_RX_CONT,
                    0U,
                    0U);

                continue;
            }

            if (info != NULL) {
                (void)parse_float_field(
                    packet,
                    "RXRSSI=",
                    &info->remote_sos_rssi_dbm);
                (void)parse_float_field(
                    packet,
                    "RXSNR=",
                    &info->remote_sos_snr_db);
            }

            return 1;
        }

        sleep_ms(RADIO_POLL_MS);
    }

    (void)sx_write3(
        spi_fd,
        REG_OP_MODE,
        OP_MODE_LONG_RANGE | OP_MODE_STDBY,
        0U,
        0U);

    return 0;
}

static void publish_lora_result(
    healink_lora_t *context,
    uint32_t event_type,
    uint32_t event_id,
    float value_b)
{
    if (context == NULL ||
        context->event_queue == (mqd_t)-1) {
        return;
    }

    healink_event_t event;
    memset(&event, 0, sizeof(event));

    event.event_type = event_type;
    event.timestamp_ns = 0ULL;
    event.value_a = (float)event_id;
    event.value_b = value_b;

    if (healink_event_publish(
            context->event_queue,
            &event,
            5U) != 0) {
        printf(
            "[LORA] Could not publish event %u\n",
            event_type);
    }
}

static uint32_t allocate_event_id(
    healink_lora_t *context)
{
    ++context->next_event_id;

    if (context->next_event_id == 0U) {
        context->next_event_id = 1U;
    }

    return context->next_event_id;
}

void *healink_lora_thread(void *thread_arg)
{
    healink_lora_t *context =
        (healink_lora_t *)thread_arg;

    if (context == NULL) {
        return NULL;
    }

    printf(
        "[LORA] worker started | SPI=%s | 433 MHz | SF7 | BW125 | CR4/5 | CRC ON | retries=%u timeout=%lu ms\n",
        LORA_SPI_DEV,
        MAX_RETRIES,
        ACK_TIMEOUT_MS);

    while (*(context->running_flag) != 0) {
        healink_lora_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));

        const ssize_t n =
            mq_receive(
                context->cmd_mq,
                (char *)&cmd,
                sizeof(cmd),
                NULL);

        if (n == (ssize_t)sizeof(cmd)) {
            if (cmd.type == LORA_CMD_SOS) {
                bool confirmed = false;
                const uint64_t command_start_ms = now_ms();

                for (unsigned int attempt = 1U;
                     attempt <= MAX_RETRIES;
                     ++attempt) {
                    char payload[128];

                    (void)snprintf(
                        payload,
                        sizeof(payload),
                        "HEALINK|SOS|NODE=QNX|EVENT=%lu|SOURCE=QNX",
                        (unsigned long)cmd.event_id);

                    const uint64_t attempt_start_ms = now_ms();
                    lora_ack_info_t ack_info;
                    memset(&ack_info, 0, sizeof(ack_info));

                    printf(
                        "[LORA] SOS TX attempt %u/%u | EVENT=%lu\n",
                        attempt,
                        MAX_RETRIES,
                        (unsigned long)cmd.event_id);

                    if (radio_tx(context->spi_fd, payload) != 0) {
                        printf(
                            "[LORA] SOS TX failure | attempt=%u\n",
                            attempt);
                        continue;
                    }

                    printf(
                        "[LORA] SOS TX complete | waiting for ACK (%lu ms)\n",
                        ACK_TIMEOUT_MS);

                    const int ack =
                        radio_receive_ack(
                            context->spi_fd,
                            cmd.event_id,
                            ACK_TIMEOUT_MS,
                            &ack_info);

                    if (ack == 1) {
                        const uint64_t rtt_ms =
                            (ack_info.rx_ts_ms >= attempt_start_ms)
                                ? (ack_info.rx_ts_ms - attempt_start_ms)
                                : 0ULL;

                        printf(
                            "[LORA] ACK CONFIRMED | EVENT=%lu | RTT=%lu ms | "
                            "ACK RSSI=%d dBm | ACK SNR=%.2f dB",
                            (unsigned long)cmd.event_id,
                            (unsigned long)rtt_ms,
                            ack_info.rssi_dbm,
                            ack_info.snr_db);

                        if (ack_info.remote_sos_rssi_dbm > -998.0f) {
                            printf(
                                " | REMOTE SOS RSSI=%.0f dBm | REMOTE SOS SNR=%.2f dB",
                                ack_info.remote_sos_rssi_dbm,
                                ack_info.remote_sos_snr_db);
                        }

                        printf("\n");

                        publish_lora_result(
                            context,
                            EVENT_LORA_ACK,
                            cmd.event_id,
                            (float)rtt_ms);

                        confirmed = true;
                        break;
                    }

                    if (ack < 0) {
                        printf(
                            "[LORA] RX error while waiting for ACK\n");
                    } else {
                        printf(
                            "[LORA] ACK timeout / non-matching packet\n");
                    }

                    if (attempt < MAX_RETRIES) {
                        printf(
                            "[LORA] retry backoff %lu ms\n",
                            RETRY_BACKOFF_MS);
                        sleep_ms(RETRY_BACKOFF_MS);
                    }
                }

                if (!confirmed) {
                    printf(
                        "[LORA] SOS ACK FAILED | EVENT=%lu\n",
                        (unsigned long)cmd.event_id);

                    const uint64_t elapsed_ms =
                        (now_ms() >= command_start_ms)
                            ? (now_ms() - command_start_ms)
                            : 0ULL;

                    publish_lora_result(
                        context,
                        EVENT_LORA_FAILURE,
                        cmd.event_id,
                        (float)elapsed_ms);
                }
            }

            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        sleep_ms(10UL);
    }

    return NULL;
}

int healink_lora_init(
    healink_lora_t *context,
    volatile sig_atomic_t *running_flag,
    mqd_t event_queue)
{
    if (context == NULL ||
        running_flag == NULL ||
        event_queue == (mqd_t)-1) {
        errno = EINVAL;
        return -1;
    }

    memset(context, 0, sizeof(*context));
    context->spi_fd = -1;
    context->event_queue = (mqd_t)-1;
    context->cmd_mq = (mqd_t)-1;

    context->spi_fd = open(
        LORA_SPI_DEV,
        O_RDWR);

    if (context->spi_fd < 0) {
        return -1;
    }

    uint8_t version = 0U;

    if (sx_read(
            context->spi_fd,
            REG_VERSION,
            &version) != 0 ||
        version != EXPECTED_VERSION) {
        close(context->spi_fd);
        context->spi_fd = -1;
        errno = ENODEV;
        return -1;
    }

    /*
     * HEALINK runs one LoRa manager.  Remove a stale command queue left by
     * an abnormal previous termination before recreating it with the current
     * message size.
     */
    (void)mq_unlink(LORA_CMD_QUEUE);

    struct mq_attr queue_attributes;
    memset(&queue_attributes, 0, sizeof(queue_attributes));

    queue_attributes.mq_maxmsg = LORA_CMD_DEPTH;
    queue_attributes.mq_msgsize = (long)sizeof(healink_lora_cmd_t);

    mqd_t cmd_mq =
        mq_open(
            LORA_CMD_QUEUE,
            O_CREAT | O_RDWR | O_NONBLOCK,
            0600,
            &queue_attributes);

    if (cmd_mq == (mqd_t)-1) {
        close(context->spi_fd);
        context->spi_fd = -1;
        return -1;
    }

    context->running_flag = running_flag;
    context->event_queue = event_queue;
    context->cmd_mq = cmd_mq;
    context->next_event_id = 0U;

    /* main() creates the worker so all thread priorities stay in one place. */
    context->initialized = true;

    return 0;
}

int healink_lora_request_sos(
    void *ctx_ptr,
    const healink_event_t *source_event)
{
    healink_lora_t *context =
        (healink_lora_t *)ctx_ptr;

    if (context == NULL ||
        !context->initialized ||
        source_event == NULL) {
        errno = EINVAL;
        return -1;
    }

    healink_lora_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.type = LORA_CMD_SOS;
    cmd.event_id = allocate_event_id(context);
    cmd.timestamp_ns = source_event->timestamp_ns;
    cmd.source_event_type = source_event->event_type;

    if (mq_send(
            context->cmd_mq,
            (const char *)&cmd,
            sizeof(cmd),
            0U) != 0) {
        return -1;
    }

    printf(
        "[LORA] SOS request queued | EVENT=%lu | source_event=%u\n",
        (unsigned long)cmd.event_id,
        source_event->event_type);

    return 0;
}

void healink_lora_shutdown(
    healink_lora_t *context)
{
    if (context == NULL) {
        return;
    }

    if (context->cmd_mq != (mqd_t)-1) {
        (void)mq_close(context->cmd_mq);
        (void)mq_unlink(LORA_CMD_QUEUE);
    }

    if (context->spi_fd >= 0) {
        (void)sx_write3(
            context->spi_fd,
            REG_OP_MODE,
            OP_MODE_LONG_RANGE | OP_MODE_SLEEP,
            0U,
            0U);

        (void)close(context->spi_fd);
    }

    memset(context, 0, sizeof(*context));
    context->spi_fd = -1;
    context->event_queue = (mqd_t)-1;
    context->cmd_mq = (mqd_t)-1;
}
