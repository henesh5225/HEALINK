#ifndef HEALINK_STATE_H
#define HEALINK_STATE_H

#include <pthread.h>

#include "healink_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {

    uint32_t version;

    pthread_mutex_t lock;

    healink_sensor_state_t data;

} healink_shared_state_t;

int healink_state_create(
    healink_shared_state_t **shared_state,
    int *shared_memory_fd);

void healink_state_destroy(
    healink_shared_state_t *shared_state,
    int shared_memory_fd,
    int unlink_object);

#ifdef __cplusplus
}
#endif

#endif /* HEALINK_STATE_H */
