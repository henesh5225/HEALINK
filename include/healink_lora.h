#ifndef HEALINK_LORA_H
#define HEALINK_LORA_H

#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <mqueue.h>

#include "healink_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int spi_fd;
    bool initialized;
    volatile sig_atomic_t *running_flag;
    mqd_t event_queue;
    mqd_t cmd_mq;
    uint32_t next_event_id;
} healink_lora_t;

/*
 * Open and initialize the QNX SX1278 SPI endpoint and start the
 * communication worker.  This is deliberately optional: a LoRa
 * bring-up failure must not stop the health-fusion core.
 */
int healink_lora_init(
    healink_lora_t *context,
    volatile sig_atomic_t *running_flag,
    mqd_t event_queue);

/*
 * Queue an SOS transmission request for the dedicated LoRa worker.
 * The worker owns the SX1278 SPI handle, sends the packet, waits for
 * a matching ACK, retries on timeout, and publishes a LoRa result
 * event back onto HEALINK's existing event queue.
 */
void *healink_lora_thread(void *arg);

int healink_lora_request_sos(
    void *context_ptr,
    const healink_event_t *source_event);

/* Stop the worker and close/unlink the internal command queue. */
void healink_lora_shutdown(healink_lora_t *context);

#ifdef __cplusplus
}
#endif

#endif /* HEALINK_LORA_H */
