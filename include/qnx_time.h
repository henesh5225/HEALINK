#ifndef QNX_TIME_H
#define QNX_TIME_H

#include <stdint.h>

typedef struct {
    uint64_t cycles;
    uint64_t active_cycles;
    uint64_t active_total_execution_ns;
    uint64_t active_max_execution_ns;
    uint64_t deadline_misses;
    uint64_t schedule_skips;
    uint64_t total_execution_ns;
    uint64_t max_execution_ns;
    uint64_t last_execution_ns;
    uint64_t max_start_jitter_ns;
    uint64_t last_start_jitter_ns;
} healink_timing_stats_t;

uint64_t healink_now_ns(void);

/*
 * Wait for the next absolute periodic release.
 *
 * Return value:
 *   0  normal wait
 *   1  one or more overdue releases were skipped
 *  -1  error
 *
 * Skipping overdue releases prevents a delayed task from entering
 * a tight catch-up loop of already-missed periods.
 */
int healink_sleep_until(uint64_t *next_release_ns, uint64_t period_ns);

void healink_timing_init(
    healink_timing_stats_t *timing_stats);

void healink_timing_begin(
    healink_timing_stats_t *timing_stats,
    uint64_t scheduled_release_ns,
    uint64_t actual_start_ns);

void healink_timing_end(
    healink_timing_stats_t *timing_stats,
    uint64_t start_ns,
    uint64_t end_ns,
    uint64_t period_ns);

#endif
