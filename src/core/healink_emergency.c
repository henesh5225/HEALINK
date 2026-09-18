#define _POSIX_C_SOURCE 200809L

#include "healink_emergency.h"

#include <errno.h>
#include <mqueue.h>
#include <stdio.h>
#include <time.h>

#include "healink_ipc.h"
#include "healink_types.h"


static void queue_lora_sos(
    healink_emergency_context_t *context,
    const healink_event_t *event)
{
    if (context == NULL || event == NULL || context->sos_callback == NULL) {
        return;
    }

    if (context->sos_callback(context->sos_callback_ctx, event) != 0) {
        printf(
            "[EMERGENCY] LoRa SOS request could not be queued | event=%u\n",
            event->event_type);
    }
}

void *healink_emergency_thread(void *thread_arg)
{
    healink_emergency_context_t *context =
        (healink_emergency_context_t *)thread_arg;

    if (context == NULL ||
        context->mq_ptr == NULL ||
        context->running_ptr == NULL) {

        return NULL;
    }

    while (*(context->running_ptr) != 0) {

        healink_event_t event;

        if (healink_event_receive(
                *(context->mq_ptr),
                &event) == 0) {

            /* UI notification is asynchronous; this thread never writes to the OLED. */
            if (context->event_callback != NULL) {
                context->event_callback(context->event_callback_ctx, &event);
            }

            switch (event.event_type) {

                case EVENT_SHUTDOWN:

                    printf(
                        "[EMERGENCY] shutdown event received\n");

                    return NULL;

                case EVENT_FALL_DETECTED:

                    printf(
                        "[EMERGENCY] FALL DETECTED | "
                        "accel=%.2fG\n",
                        event.value_a);

                    printf(
                        "[EMERGENCY] SOS path activated\n");
printf("[EMERGENCY] LoRa TX disabled for automatic sensor events | source=FALL_DETECTED\n");

                    break;

                case EVENT_EMERGENCY:

                    printf(
                        "[EMERGENCY] EMERGENCY | "
                        "A=%.2f B=%.2f\n",
                        event.value_a,
                        event.value_b);
printf("[EMERGENCY] LoRa TX disabled for automatic sensor events | source=EMERGENCY\n");

                    break;

                case EVENT_DISTRESS:

                    printf(
                        "[EMERGENCY] DISTRESS | "
                        "HR=%.1f SpO2=%.1f\n",
                        event.value_a,
                        event.value_b);
printf("[EMERGENCY] LoRa TX disabled for automatic sensor events | source=DISTRESS\n");

                    break;

                case EVENT_SENSOR_FAULT:

                    printf(
                        "[EMERGENCY] SENSOR FAULT | "
                        "mask=0x%08X\n",
                        event.sensor_mask);

                    break;

                case EVENT_SENSOR_STALE:

                    printf(
                        "[EMERGENCY] SENSOR STALE | "
                        "mask=0x%08X\n",
                        event.sensor_mask);

                    break;

                case EVENT_STALE_DATA:

                    printf(
                        "[EMERGENCY] STALE DATA | "
                        "mask=0x%08X\n",
                        event.sensor_mask);

                    break;

                case EVENT_INCONSISTENT:

                    printf(
                        "[EMERGENCY] INCONSISTENCY | "
                        "mask=0x%08X delta=%.2f "
                        "confidence=%.2f\n",
                        event.sensor_mask,
                        event.value_a,
                        event.value_b);

                    break;

                case EVENT_STATE_CHANGE:

                    printf(
                        "[EMERGENCY] STATE CHANGE | "
                        "state=%.0f confidence=%.2f\n",
                        event.value_a,
                        event.value_b);

                    break;

                case EVENT_LOW_BATTERY:

                    printf(
                        "[EMERGENCY] LOW BATTERY\n");

                    break;

                case EVENT_SOS:

                    printf(
                        "[EMERGENCY] SOS EVENT\n");

                    queue_lora_sos(context, &event);

                    break;

                case EVENT_LORA_ACK:

                    printf(
                        "[EMERGENCY] LORA ACK CONFIRMED | "
                        "event=%.0f rtt=%.0f ms\n",
                        event.value_a,
                        event.value_b);

                    break;

                case EVENT_LORA_FAILURE:

                    printf(
                        "[EMERGENCY] LORA ACK FAILURE | "
                        "event=%.0f elapsed=%.0f ms\n",
                        event.value_a,
                        event.value_b);

                    break;

                case EVENT_ALERT:

                    printf(
                        "[EMERGENCY] ALERT EVENT\n");

                    break;

                case EVENT_RECOVERY:

                    printf(
                        "[EMERGENCY] RECOVERY EVENT\n");

                    break;

                default:

                    printf(
                        "[EMERGENCY] UNKNOWN EVENT %u\n",
                        event.event_type);

                    break;
            }

            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        /*
         * The receive path is nonblocking in this shutdown-aware
         * worker.  Wake periodically so SIGINT/SIGTERM can always
         * terminate the thread even when no queue message arrives.
         */
        {
            struct timespec ts;

            ts.tv_sec = 0;
            ts.tv_nsec = 10000000L;

            (void)nanosleep(&ts, NULL);
        }
    }

    return NULL;
}
