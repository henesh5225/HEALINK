#ifndef HEALINK_EMERGENCY_H
#define HEALINK_EMERGENCY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mqueue.h>
#include <signal.h>

#include "healink_types.h"

typedef int (*healink_emergency_sos_callback_t)(
    void *context,
    const healink_event_t *event_message);

typedef void (*healink_emergency_event_callback_t)(
    void *context,
    const healink_event_t *event_message);

typedef struct {
    mqd_t *mq_ptr;
    volatile sig_atomic_t *running_ptr;
    healink_emergency_sos_callback_t sos_callback;
    void *sos_callback_ctx;
    healink_emergency_event_callback_t event_callback;
    void *event_callback_ctx;
} healink_emergency_context_t;

void *healink_emergency_thread(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* HEALINK_EMERGENCY_H */
