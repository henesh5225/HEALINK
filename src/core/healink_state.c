#define _POSIX_C_SOURCE 200809L

#include "healink_state.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * ============================================================
 * HEALINK SHARED MEMORY OBJECT
 * ============================================================
 *
 * The name must begin with '/' for POSIX shared memory.
 *
 * Keep this name stable because every process that wants to
 * attach to the HEALINK shared state must use the same object.
 */
#define HEALINK_SHM_NAME "/healink_state"

/* CREATE SHARED STATE */

int healink_state_create(
    healink_shared_state_t **out_state,
    int *out_shm_fd)
{
    int shared_memory_fd;
    void *mapped_address;
    pthread_mutexattr_t mutex_attributes;
    int result;

    if (out_state == NULL ||
        out_shm_fd == NULL) {

        errno = EINVAL;
        return -1;
    }

    *out_state = NULL;
    *out_shm_fd = -1;

    /*
     * Create/open the POSIX shared-memory object.
     */
    shared_memory_fd = shm_open(
        HEALINK_SHM_NAME,
        O_CREAT | O_RDWR,
        0660);

    if (shared_memory_fd < 0) {
        return -1;
    }

    /*
     * Ensure the object is exactly large enough for the
     * complete shared-state structure.
     */
    if (ftruncate(
            shared_memory_fd,
            (off_t)sizeof(healink_shared_state_t)) != 0) {

        int saved_errno = errno;

        (void)close(shared_memory_fd);

        errno = saved_errno;
        return -1;
    }

    /*
     * Map the shared memory into this process.
     */
    mapped_address = mmap(
        NULL,
        sizeof(healink_shared_state_t),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        shared_memory_fd,
        0);

    if (mapped_address == MAP_FAILED) {

        int saved_errno = errno;

        (void)close(shared_memory_fd);

        errno = saved_errno;
        return -1;
    }

    *out_state =
        (healink_shared_state_t *)mapped_address;

    *out_shm_fd = shared_memory_fd;

    /*
     * Initialize the complete object.
     */
    memset(
        *out_state,
        0,
        sizeof(**out_state));

    /*
     * Shared-state structure version.
     */
    (*out_state)->version = 2U;

    /*
     * Configure mutex for use by multiple processes.
     */
    result = pthread_mutexattr_init(&mutex_attributes);

    if (result != 0) {

        (void)munmap(
            *out_state,
            sizeof(healink_shared_state_t));

        (void)close(shared_memory_fd);

        *out_state = NULL;
        *out_shm_fd = -1;

        errno = result;
        return -1;
    }

    result = pthread_mutexattr_setpshared(
        &mutex_attributes,
        PTHREAD_PROCESS_SHARED);

    if (result != 0) {

        (void)pthread_mutexattr_destroy(
            &mutex_attributes);

        (void)munmap(
            *out_state,
            sizeof(healink_shared_state_t));

        (void)close(shared_memory_fd);

        *out_state = NULL;
        *out_shm_fd = -1;

        errno = result;
        return -1;
    }

    result = pthread_mutex_init(
        &(*out_state)->lock,
        &mutex_attributes);

    (void)pthread_mutexattr_destroy(
        &mutex_attributes);

    if (result != 0) {

        (void)munmap(
            *out_state,
            sizeof(healink_shared_state_t));

        (void)close(shared_memory_fd);

        *out_state = NULL;
        *out_shm_fd = -1;

        errno = result;
        return -1;
    }

    /*
     * Explicit initial state.
     */
    (*out_state)->data.health_state =
        HEALTH_UNKNOWN;

    (*out_state)->data.activity =
        ACTIVITY_UNKNOWN;

    (*out_state)->data.valid_mask = 0U;
    (*out_state)->data.stale_mask = 0U;
    (*out_state)->data.offline_mask = 0U;
    (*out_state)->data.fault_mask = 0U;
    (*out_state)->data.inconsistent_mask = 0U;

    (*out_state)->data.overall_confidence =
        0.0f;

    (*out_state)->data.hr_confidence =
        0.0f;

    (*out_state)->data.ecg_quality = 0.0f;
    (*out_state)->data.ppg_quality = 0.0f;
    (*out_state)->data.hr_stability = 0.0f;
    (*out_state)->data.previous_hr_bpm = 0.0f;
    (*out_state)->data.hr_history_valid = false;
    (*out_state)->data.anomaly_score = 0.0f;
    (*out_state)->data.fall_confidence = 0.0f;

    (*out_state)->data.spo2_confidence =
        0.0f;

    (*out_state)->data.temperature_confidence =
        0.0f;

    (*out_state)->data.motion_confidence =
        0.0f;

    (*out_state)->data.ecg_ppg_confidence =
        0.0f;

    (*out_state)->data.fall_event =
        false;

    (*out_state)->data.last_fall_ts_ns =
        0U;
    (*out_state)->data.emergency_latched = false;
    (*out_state)->data.alert_latched = false;
    (*out_state)->data.distress_confirm_count = 0U;
    (*out_state)->data.recovery_confirm_count = 0U;

    return 0;
}

/* DESTROY SHARED STATE */

void healink_state_destroy(
    healink_shared_state_t *state,
    int shm_fd,
    int unlink_object)
{
    if (state != NULL) {

        /*
         * Destroy the process-shared mutex before unmapping.
         */
        (void)pthread_mutex_destroy(
            &state->lock);

        (void)munmap(
            state,
            sizeof(*state));
    }

    if (shm_fd >= 0) {
        (void)close(shm_fd);
    }

    if (unlink_object != 0) {
        (void)shm_unlink(
            HEALINK_SHM_NAME);
    }
}
