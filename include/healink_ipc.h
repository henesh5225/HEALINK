#ifndef HEALINK_IPC_H
#define HEALINK_IPC_H

#include <mqueue.h>

#include "healink_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int healink_event_queue_open(
    mqd_t *event_queue,
    int create_queue);

int healink_event_publish(
    mqd_t event_queue,
    const healink_event_t *event_message,
    unsigned timeout_ms);

int healink_event_receive(
    mqd_t event_queue,
    healink_event_t *event_message);

void healink_event_queue_close(
    mqd_t event_queue,
    int unlink_queue);

#ifdef __cplusplus
}
#endif

#endif /* HEALINK_IPC_H */
