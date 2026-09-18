#define _POSIX_C_SOURCE 200809L

#include "qnx_time.h"

#include <errno.h>
#include <stdint.h>
#include <time.h>

static uint64_t abs_diff_u64(uint64_t a, uint64_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

uint64_t healink_now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }

    return ((uint64_t)ts.tv_sec * 1000000000ULL) +
           (uint64_t)ts.tv_nsec;
}

int healink_sleep_until(uint64_t *next_release_ns,
                        uint64_t period_ns)
{
    struct timespec target;
    uint64_t now_ns;
    uint64_t target_ns;
    uint64_t skipped = 0ULL;

    if (next_release_ns == NULL || period_ns == 0ULL) {
        errno = EINVAL;
        return -1;
    }

    now_ns = healink_now_ns();

    if (now_ns == 0ULL) {
        errno = EIO;
        return -1;
    }

    if (*next_release_ns == 0ULL) {
        *next_release_ns = now_ns;
    }

    if (*next_release_ns > UINT64_MAX - period_ns) {
        errno = ERANGE;
        return -1;
    }

    target_ns = *next_release_ns + period_ns;

    /*
     * The previous implementation could repeatedly sleep on already
     * expired absolute deadlines after a long execution delay. That
     * creates a catch-up loop and wastes CPU exactly when the system is
     * already overloaded.
     *
     * Instead, skip all releases that are already in the past and jump
     * directly to the first future release.
     */
    if (target_ns <= now_ns) {
        uint64_t behind_ns = now_ns - target_ns;
        uint64_t periods_behind =
            (behind_ns / period_ns) + 1ULL;

        if (periods_behind >
            (UINT64_MAX - *next_release_ns) / period_ns) {
            errno = ERANGE;
            return -1;
        }

        target_ns =
            *next_release_ns +
            (periods_behind * period_ns);

        skipped = periods_behind;
    }

    *next_release_ns = target_ns;

    target.tv_sec =
        (time_t)(target_ns / 1000000000ULL);

    target.tv_nsec =
        (long)(target_ns % 1000000000ULL);

    if (clock_nanosleep(CLOCK_MONOTONIC,
                        (int)TIMER_ABSTIME,
                        &target,
                        NULL) != 0) {
        return -1;
    }

    return (skipped != 0ULL) ? 1 : 0;
}

void healink_timing_init(
    healink_timing_stats_t *timing_stats)
{
    if (timing_stats == NULL) {
        return;
    }

    *timing_stats = (healink_timing_stats_t){0};
}

void healink_timing_begin(
    healink_timing_stats_t *timing_stats,
    uint64_t scheduled_release_ns,
    uint64_t actual_start_ns)
{
    uint64_t jitter_ns;

    if (timing_stats == NULL) {
        return;
    }

    timing_stats->cycles++;

    jitter_ns =
        abs_diff_u64(
            actual_start_ns,
            scheduled_release_ns);

    timing_stats->last_start_jitter_ns = jitter_ns;

    if (jitter_ns > timing_stats->max_start_jitter_ns) {
        timing_stats->max_start_jitter_ns = jitter_ns;
    }
}

void healink_timing_end(
    healink_timing_stats_t *timing_stats,
    uint64_t start_ns,
    uint64_t end_ns,
    uint64_t period_ns)
{
    uint64_t execution_ns;

    if (timing_stats == NULL) {
        return;
    }

    if (end_ns < start_ns) {
        return;
    }

    execution_ns = end_ns - start_ns;

    timing_stats->last_execution_ns = execution_ns;
    timing_stats->total_execution_ns += execution_ns;

    if (execution_ns > timing_stats->max_execution_ns) {
        timing_stats->max_execution_ns = execution_ns;
    }

    if (period_ns != 0ULL &&
        execution_ns > period_ns) {
        timing_stats->deadline_misses++;
    }
}
