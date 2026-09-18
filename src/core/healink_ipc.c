#define _POSIX_C_SOURCE 200809L

#include "healink_ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#define HEALINK_EVENT_QUEUE_NAME "/healink_events"
#define HEALINK_EVENT_QUEUE_DEPTH 64

/*
 * The receiver and publisher use different descriptors so that the
 * real-time workers can publish without ever blocking on a full queue.
 */
static mqd_t g_publisher_queue = (mqd_t)-1;

int healink_event_queue_open(
    mqd_t *event_queue,
    int create_queue)
{
    struct mq_attr queue_attributes;
    int flags = O_RDWR;

    if (event_queue == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (create_queue != 0) {
        flags |= O_CREAT;

        /* Start every HEALINK run with an empty event queue. */
        if (mq_unlink(HEALINK_EVENT_QUEUE_NAME) != 0 &&
            errno != ENOENT) {
            return -1;
        }
    }

    memset(&queue_attributes, 0, sizeof(queue_attributes));

    queue_attributes.mq_maxmsg = HEALINK_EVENT_QUEUE_DEPTH;
    queue_attributes.mq_msgsize = (long)sizeof(healink_event_t);

    if (create_queue != 0) {
        *event_queue = mq_open(
            HEALINK_EVENT_QUEUE_NAME,
            flags,
            0600,
            &queue_attributes);
    } else {
        *event_queue = mq_open(
            HEALINK_EVENT_QUEUE_NAME,
            flags);
    }

    if (*event_queue == (mqd_t)-1) {
        return -1;
    }

    /*
     * Separate nonblocking publisher descriptor.
     * This prevents sensor/fusion/emergency producers from sleeping
     * behind a full diagnostic queue.
     */
    g_publisher_queue = mq_open(
        HEALINK_EVENT_QUEUE_NAME,
        O_WRONLY | O_NONBLOCK);

    if (g_publisher_queue == (mqd_t)-1) {
        int saved_errno = errno;

        (void)mq_close(*event_queue);
        *event_queue = (mqd_t)-1;

        if (create_queue != 0) {
            (void)mq_unlink(HEALINK_EVENT_QUEUE_NAME);
        }

        errno = saved_errno;
        return -1;
    }

    return 0;
}

int healink_event_publish(
    mqd_t event_queue,
    const healink_event_t *event,
    unsigned timeout_ms)
{
    (void)timeout_ms;

    if (event == NULL || event_queue == (mqd_t)-1) {
        errno = EINVAL;
        return -1;
    }

    if (g_publisher_queue == (mqd_t)-1) {
        errno = ENOTCONN;
        return -1;
    }

    /*
     * Always use the nonblocking publisher descriptor.
     * EAGAIN means the diagnostic queue is saturated; the calling
     * real-time worker must continue rather than wait for consumers.
     */
    return mq_send(
        g_publisher_queue,
        (const char *)event,
        sizeof(*event),
        0U);
}

int healink_event_receive(
    mqd_t event_queue,
    healink_event_t *event)
{
    ssize_t bytes_received;

    if (event == NULL || event_queue == (mqd_t)-1) {
        errno = EINVAL;
        return -1;
    }

    bytes_received = mq_receive(
        event_queue,
        (char *)event,
        sizeof(*event),
        NULL);

    if (bytes_received < 0) {
        return -1;
    }

    if ((size_t)bytes_received != sizeof(*event)) {
        errno = EMSGSIZE;
        return -1;
    }

    return 0;
}

void healink_event_queue_close(
    mqd_t event_queue,
    int unlink_queue)
{
    if (g_publisher_queue != (mqd_t)-1) {
        (void)mq_close(g_publisher_queue);
        g_publisher_queue = (mqd_t)-1;
    }

    if (event_queue != (mqd_t)-1) {
        (void)mq_close(event_queue);
    }

    if (unlink_queue != 0) {
        (void)mq_unlink(HEALINK_EVENT_QUEUE_NAME);
    }
}
