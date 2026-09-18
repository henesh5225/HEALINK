#define _POSIX_C_SOURCE 200809L

#include "healink_config.h"
#include "healink_state.h"
#include "healink_ipc.h"
#include "healink_fusion.h"
#include "healink_classifier.h"
#include "healink_staleness.h"
#include "healink_emergency.h"
#include "healink_lora.h"
#include "healink_safety.h"
#include "qnx_time.h"
#include "qnx_i2c.h"
#include "rpi_gpio.h"
#include "max30102.h"
#include "ads1115.h"
#include "ad8232.h"
#include "mpu6050.h"
#include "ds18b20.h"
#include "dht11.h"
#include "ssd1306.h"
#include "healink_ui.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static healink_shared_state_t *g_shared_state = NULL;
static int g_shared_memory_fd = -1;

static mqd_t g_event_queue = (mqd_t)-1;

static healink_i2c_t g_i2c_bus;
static bool g_i2c_ready = false;

static ssd1306_t g_oled_display;
static healink_ui_t g_user_interface;
static bool g_demo_start_enabled = false;
static bool g_terminal_raw_enabled = false;
static struct termios g_terminal_saved;
static max30102_t g_max30102_sensor;
static ads1115_t g_ads1115_adc;
static ad8232_t g_ad8232_ecg;
static mpu6050_t g_mpu6050_sensor;
static ds18b20_t g_ds18b20_sensor;
static dht11_t g_dht11_sensor;

static bool g_max30102_ready = false;
static bool g_ads1115_ready = false;
static bool g_ad8232_ready = false;
static bool g_mpu6050_ready = false;
static bool g_ds18b20_ready = false;
static bool g_dht11_ready = false;
static bool g_oled_ready = false;

static pthread_t g_max30102_thread;
static pthread_t g_ad8232_thread;
static pthread_t g_mpu6050_thread;
static pthread_t g_ds18b20_thread;
static pthread_t g_dht11_thread;
static pthread_t g_fusion_thread;
static pthread_t g_ui_thread;
static pthread_t g_emergency_thread;
static pthread_t g_sos_button_thread;
static pthread_t g_lora_thread;

static bool g_max30102_thread_started = false;
static bool g_ad8232_thread_started = false;
static bool g_mpu6050_thread_started = false;
static bool g_ds18b20_thread_started = false;
static bool g_dht11_thread_started = false;
static bool g_fusion_thread_started = false;
static bool g_ui_thread_started = false;
static bool g_emergency_thread_started = false;
static bool g_sos_button_thread_started = false;
static bool g_lora_thread_started = false;

static healink_emergency_context_t g_emergency_context;
static healink_lora_t g_lora_context;
static bool g_lora_ready = false;

static void print_timing_stats(
    const char *name,
    const healink_timing_stats_t *stats)
{
    if (stats == NULL || stats->cycles == 0ULL) {
        return;
    }

    if (stats->active_cycles == 0ULL) {
        printf(
            "[TIMING] %-9s cycles=%" PRIu64
            " active=0 avg_exec=N/A max_exec=N/A"
            " max_jitter=%" PRIu64 "us"
            " deadline_miss=%" PRIu64
            " schedule_skip=%" PRIu64 "\n",
            name,
            stats->cycles,
            stats->max_start_jitter_ns / 1000U,
            stats->deadline_misses,
            stats->schedule_skips);
        return;
    }

    {
        uint64_t average_execution_ns =
            stats->active_total_execution_ns /
            stats->active_cycles;

        printf(
            "[TIMING] %-9s cycles=%" PRIu64
            " active=%" PRIu64
            " active_avg_exec=%" PRIu64 "us"
            " active_max_exec=%" PRIu64 "us"
            " max_jitter=%" PRIu64 "us"
            " deadline_miss=%" PRIu64
            " schedule_skip=%" PRIu64 "\n",
            name,
            stats->cycles,
            stats->active_cycles,
            average_execution_ns / 1000U,
            stats->active_max_execution_ns / 1000U,
            stats->max_start_jitter_ns / 1000U,
            stats->deadline_misses,
            stats->schedule_skips);
    }
}


static void on_signal(int signo)
{
    (void)signo;
    g_running = 0;
}


static void state_lock(void)
{
    if (g_shared_state != NULL) {
        (void)pthread_mutex_lock(&g_shared_state->lock);
    }
}

static void state_unlock(void)
{
    if (g_shared_state != NULL) {
        (void)pthread_mutex_unlock(&g_shared_state->lock);
    }
}


static void publish_event(
    uint32_t type,
    uint32_t sensor_mask,
    float a,
    float b)
{
    healink_event_t event;

    if (g_event_queue == (mqd_t)-1) {
        return;
    }

    event.event_type = type;
    event.sensor_mask = sensor_mask;
    event.timestamp_ns = healink_now_ns();
    event.value_a = a;
    event.value_b = b;

    if (healink_event_publish(
            g_event_queue,
            &event,
            (type == EVENT_FALL_DETECTED ||
             type == EVENT_EMERGENCY ||
             type == EVENT_SHUTDOWN ||
             type == EVENT_DISTRESS ||
             type == EVENT_SOS) ? 10U : 5U) != 0) {

        if (type == EVENT_EMERGENCY ||
            type == EVENT_FALL_DETECTED ||
            type == EVENT_DISTRESS) {
            printf(
                "[HEALINK] CRITICAL EVENT QUEUE SATURATED | type=%u\n",
                type);
        }
    }
}

/* Send the event to the UI thread without doing OLED I/O here. */
static void notify_ui_event(
    void *context,
    const healink_event_t *event)
{
    if (context == NULL || event == NULL) {
        return;
    }

    healink_ui_notify_event(
        (healink_ui_t *)context,
        event,
        healink_now_ns());
}

/*
 * ============================================================
 * SENSOR QUALITY STATE
 * ============================================================
 *
 * OFFLINE:
 *     Sensor could not be initialized.
 *
 * VALID:
 *     Sensor produced a usable measurement.
 *
 * STALE:
 *     Sensor has not updated within its temporal deadline.
 *
 * FAULT:
 *     Sensor was expected to operate, but a runtime read/
 *     communication operation failed.
 *
 * RECOVERY:
 *     A previously FAULT or STALE sensor successfully
 *     communicates again.
 */

/*
 * ------------------------------------------------------------
 * Mark sensor offline during initialization
 * ------------------------------------------------------------
 */

static void mark_sensor_offline(uint32_t sensor_mask)
{
    if (g_shared_state == NULL) {
        return;
    }

    state_lock();

    g_shared_state->data.offline_mask |= sensor_mask;

    /*
     * OFFLINE is a lifecycle state, not stale or runtime fault.
     */
    g_shared_state->data.valid_mask &= ~sensor_mask;
    g_shared_state->data.stale_mask &= ~sensor_mask;
    g_shared_state->data.fault_mask &= ~sensor_mask;

    state_unlock();
}

/*
 * ------------------------------------------------------------
 * Mark sensor online
 * ------------------------------------------------------------
 */

static void mark_sensor_online(uint32_t sensor_mask)
{
    if (g_shared_state == NULL) {
        return;
    }

    state_lock();

    g_shared_state->data.offline_mask &= ~sensor_mask;

    state_unlock();
}

/*
 * ------------------------------------------------------------
 * Mark runtime fault
 * ------------------------------------------------------------
 */

static void mark_sensor_fault(uint32_t sensor_mask)
{
    bool new_fault = false;

    if (g_shared_state == NULL) {
        return;
    }

    state_lock();

    /*
     * An intentionally unavailable/offline sensor is not a
     * runtime fault.
     */
    if ((g_shared_state->data.offline_mask & sensor_mask) == 0U) {

        if ((g_shared_state->data.fault_mask & sensor_mask) == 0U) {
            new_fault = true;
        }

        g_shared_state->data.fault_mask |= sensor_mask;

        /*
         * FAULT supersedes freshness until communication recovers.
         */
        g_shared_state->data.stale_mask &= ~sensor_mask;
        g_shared_state->data.valid_mask &= ~sensor_mask;
    }

    state_unlock();

    /*
     * Only transition into FAULT generates an event.
     */
    if (new_fault) {

        publish_event(
            EVENT_SENSOR_FAULT,
            sensor_mask,
            0.0f,
            0.0f);
    }
}

/*
 * ------------------------------------------------------------
 * Mark successful runtime communication
 * ------------------------------------------------------------
 *
 * A successful communication clears a previous runtime fault.
 *
 * The valid argument describes whether the resulting measurement
 * itself is usable.
 */

static void mark_sensor_success(
    uint32_t sensor_mask,
    bool valid)
{
    bool recovered = false;
    bool was_stale = false;

    if (g_shared_state == NULL) {
        return;
    }

    state_lock();

    /*
     * Successful communication means the sensor is available.
     */
    g_shared_state->data.offline_mask &= ~sensor_mask;

    /*
     * Successful communication can recover either a runtime
     * FAULT or a previously STALE sensor.
     */
    if ((g_shared_state->data.fault_mask & sensor_mask) != 0U) {

        recovered = true;

        g_shared_state->data.fault_mask &= ~sensor_mask;
    }

    if ((g_shared_state->data.stale_mask & sensor_mask) != 0U) {

        was_stale = true;
        recovered = true;

        g_shared_state->data.stale_mask &= ~sensor_mask;
    }

    /*
     * Measurement validity is independent of communication
     * success.
     */
    if (valid) {

        g_shared_state->data.valid_mask |= sensor_mask;

    } else {

        g_shared_state->data.valid_mask &= ~sensor_mask;
    }

    state_unlock();

    /*
     * Only a real FAULT/STALE -> recovered transition generates
     * one recovery event.
     *
     * value_a:
     *     1 = resulting measurement is valid
     *     0 = communication recovered but measurement unusable
     *
     * value_b:
     *     1 = STALE recovery
     *     0 = FAULT recovery
     */
    if (recovered) {

        publish_event(
            EVENT_RECOVERY,
            sensor_mask,
            valid ? 1.0f : 0.0f,
            was_stale ? 1.0f : 0.0f);
    }
}


static void *max30102_thread(void *thread_arg)
{
    uint64_t next_wakeup_ns = healink_now_ns();

    (void)thread_arg;

    while (g_running) {

        if (g_max30102_ready) {

            uint64_t current_time_ns = healink_now_ns();

            if (max30102_read_sample(
                    &g_max30102_sensor,
                    current_time_ns) == 0) {

                float hr = 0.0f;
                float spo2 = 0.0f;
                float ppg_quality = 0.0f;
                bool valid = false;
                uint64_t ts = 0U;

                max30102_get(
                    &g_max30102_sensor,
                    &hr,
                    &spo2,
                    &ppg_quality,
                    &valid,
                    &ts);

                state_lock();

                g_shared_state->data.ppg_hr_bpm = hr;
                g_shared_state->data.spo2_pct = spo2;
                g_shared_state->data.ppg_quality = ppg_quality;
                g_shared_state->data.max30102_ts_ns = ts;

                state_unlock();

                mark_sensor_success(
                    SENSOR_MAX30102,
                    valid);

            } else {

                /* EAGAIN means the 100 Hz task observed no new FIFO sample yet.
                 * It is not a runtime hardware fault; the staleness monitor owns
                 * freshness decisions. Real I/O/overflow failures remain faults. */
                if (errno != EAGAIN) {
                    mark_sensor_fault(
                        SENSOR_MAX30102);
                }
            }
        }

        (void)healink_sleep_until(
            &next_wakeup_ns,
            HEALINK_PERIOD_MAX30102_NS);
    }

    return NULL;
}


static void *ad8232_thread(void *thread_arg)
{
    uint64_t next_wakeup_ns = healink_now_ns();
    healink_timing_stats_t timing;

    healink_timing_init(&timing);

    (void)thread_arg;

    while (g_running) {

        uint64_t scheduled_release_ns = next_wakeup_ns;
        uint64_t cycle_start_ns = healink_now_ns();
        bool is_active_cycle = false;

        healink_timing_begin(
            &timing,
            scheduled_release_ns,
            cycle_start_ns);

        if (g_ad8232_ready) {
            is_active_cycle = true;
            timing.active_cycles++;

            uint64_t current_time_ns = cycle_start_ns;

            if (ad8232_sample(
                    &g_ad8232_ecg,
                    current_time_ns) == 0) {

                float bpm = 0.0f;
                float ecg_quality = 0.0f;
                bool valid = false;
                bool lead_status_available = false;
                bool leads_on = true;
                uint64_t ts = 0U;

                ad8232_get(
                    &g_ad8232_ecg,
                    &bpm,
                    &ecg_quality,
                    &valid,
                    &lead_status_available,
                    &leads_on,
                    &ts);

                state_lock();

                g_shared_state->data.ecg_hr_bpm = bpm;
                g_shared_state->data.ecg_quality = ecg_quality;
                g_shared_state->data.ad8232_ts_ns = ts;

                state_unlock();

                /*
                 * Communication succeeded.
                 *
                 * Lead-off or insufficient ECG signal means the
                 * measurement is not currently usable, but this
                 * alone is not an I/O communication fault.
                 */
                mark_sensor_success(
                    SENSOR_AD8232,
                    valid && (!lead_status_available || leads_on));

            } else {

                mark_sensor_fault(
                    SENSOR_AD8232);
            }
        }

        {
            uint64_t cycle_end_ns = healink_now_ns();
            int sleep_rc;

            healink_timing_end(
                &timing,
                cycle_start_ns,
                cycle_end_ns,
                HEALINK_PERIOD_AD8232_NS);

            if (is_active_cycle) {
                uint64_t execution_ns =
                    (cycle_end_ns >= cycle_start_ns)
                        ? (cycle_end_ns - cycle_start_ns)
                        : 0ULL;

                timing.active_total_execution_ns +=
                    execution_ns;

                if (execution_ns >
                    timing.active_max_execution_ns) {
                    timing.active_max_execution_ns =
                        execution_ns;
                }
            }

            sleep_rc = healink_sleep_until(
                &next_wakeup_ns,
                HEALINK_PERIOD_AD8232_NS);

            if (sleep_rc > 0) {
                timing.schedule_skips += 1ULL;
            }

            if ((timing.cycles % 250ULL) == 0ULL &&
                timing.cycles != 0ULL) {
                print_timing_stats(
                    "AD8232",
                    &timing);
            }
        }
    }

    return NULL;
}


static void *mpu6050_thread(void *thread_arg)
{
    uint64_t next_wakeup_ns = healink_now_ns();

    (void)thread_arg;

    while (g_running) {

        if (g_mpu6050_ready) {

            uint64_t current_time_ns = healink_now_ns();

            if (mpu6050_read(
                    &g_mpu6050_sensor,
                    current_time_ns) == 0) {

                bool fall;

                state_lock();

                fall = g_mpu6050_sensor.fall_event;

                g_shared_state->data.accel_x_g =
                    g_mpu6050_sensor.ax_g;

                g_shared_state->data.accel_y_g =
                    g_mpu6050_sensor.ay_g;

                g_shared_state->data.accel_z_g =
                    g_mpu6050_sensor.az_g;

                g_shared_state->data.accel_mag_g =
                    g_mpu6050_sensor.mag_g;

                g_shared_state->data.mpu6050_ts_ns =
                    g_mpu6050_sensor.last_ts_ns;

                state_unlock();

                mark_sensor_success(
                    SENSOR_MPU6050,
                    g_mpu6050_sensor.valid);

                if (fall) {

                    state_lock();

                    g_shared_state->data.fall_event = true;

                    g_shared_state->data.last_fall_ts_ns =
                        current_time_ns;

                    g_shared_state->data.fall_confidence = 1.0f;

                    state_unlock();

                    publish_event(
                        EVENT_FALL_DETECTED,
                        SENSOR_MPU6050,
                        g_mpu6050_sensor.mag_g,
                        0.0f);
                }

            } else {

                mark_sensor_fault(
                    SENSOR_MPU6050);
            }
        }

        (void)healink_sleep_until(
            &next_wakeup_ns,
            HEALINK_PERIOD_MPU6050_NS);
    }

    return NULL;
}


static void *ds18b20_thread(void *thread_arg)
{
    uint64_t next_wakeup_ns = healink_now_ns();

    (void)thread_arg;

    while (g_running) {

        if (g_ds18b20_ready) {

            uint64_t current_time_ns = healink_now_ns();

            if (ds18b20_read(
                    &g_ds18b20_sensor,
                    current_time_ns) == 0 &&
                g_ds18b20_sensor.valid) {

                /*
                 * The DS18B20 read includes the conversion interval and
                 * the scratchpad transaction. Timestamp the sample when
                 * the complete measurement becomes available, not at the
                 * start of the conversion. This prevents a valid 1 Hz
                 * sensor from appearing stale merely because the 12-bit
                 * conversion consumed most of the acquisition period.
                 */
                g_ds18b20_sensor.last_ts_ns = healink_now_ns();

                state_lock();

                g_shared_state->data.temperature_c =
                    g_ds18b20_sensor.temperature_c;

                g_shared_state->data.ds18b20_ts_ns =
                    g_ds18b20_sensor.last_ts_ns;

                state_unlock();

                mark_sensor_success(
                    SENSOR_DS18B20,
                    true);

            } else {

                mark_sensor_fault(
                    SENSOR_DS18B20);
            }
        }

        (void)healink_sleep_until(
            &next_wakeup_ns,
            HEALINK_PERIOD_DS18B20_NS);
    }

    return NULL;
}


static void *dht11_thread(void *thread_arg)
{
    /* Do not hit a freshly-powered DHT11 immediately. The sensor needs
     * settling time, and a single timing miss must not become a system
     * fault. */
    uint64_t next_wakeup_ns = healink_now_ns() + HEALINK_PERIOD_DHT11_NS;
    unsigned consecutive_failures = 0U;
    const unsigned fault_after = 3U;

    (void)thread_arg;

    while (g_running) {
        if (g_dht11_ready) {
            uint64_t current_time_ns = healink_now_ns();
            int read_rc = dht11_read(&g_dht11_sensor, current_time_ns);

            if (read_rc == DHT11_OK && g_dht11_sensor.valid) {

                consecutive_failures = 0U;

                /* Timestamp after the complete wire transaction, matching
                 * the proven DS18B20 timestamp semantics. */
                g_dht11_sensor.last_ts_ns = healink_now_ns();

                state_lock();

                g_shared_state->data.ambient_temperature_c =
                    (float)g_dht11_sensor.temperature_c;
                g_shared_state->data.humidity_pct =
                    (float)g_dht11_sensor.humidity_pct;
                g_shared_state->data.dht11_ts_ns =
                    g_dht11_sensor.last_ts_ns;

                state_unlock();

                mark_sensor_success(
                    SENSOR_DHT11,
                    true);

            } else {
                if (consecutive_failures < fault_after) {
                    ++consecutive_failures;
                }

                printf(
                    "[DHT11] read failed result=%d consecutive=%u/%u\n",
                    read_rc,
                    consecutive_failures,
                    fault_after);

                /* DHT11 is timing-sensitive. Only promote a persistent
                 * communication failure to FAULT; one missed frame is a
                 * transient and the next_wakeup_ns scheduled sample can recover. */
                if (consecutive_failures >= fault_after) {
                    mark_sensor_fault(SENSOR_DHT11);
                }
            }
        }

        (void)healink_sleep_until(
            &next_wakeup_ns,
            HEALINK_PERIOD_DHT11_NS);
    }

    return NULL;
}

static void *fusion_thread(void *thread_arg)
{
    uint64_t next_wakeup_ns = healink_now_ns();
    healink_timing_stats_t timing;

    healink_timing_init(&timing);

    healink_health_state_t previous_state =
        HEALTH_UNKNOWN;

    /*
     * A3:
     *
     * Remember the previous stale mask so that we generate
     * EVENT_SENSOR_STALE only on the transition into STALE.
     *
     * Without this variable, every fusion cycle would either
     * repeatedly report the same stale condition or fail to
     * compile because the transition state would be undefined.
     */
    uint32_t previous_stale_mask = 0U;

    (void)thread_arg;

    while (g_running) {

        healink_sensor_state_t snapshot;
        uint64_t scheduled_release_ns = next_wakeup_ns;
        uint64_t cycle_start_ns = healink_now_ns();
        uint64_t current_time_ns = cycle_start_ns;

        healink_timing_begin(
            &timing,
            scheduled_release_ns,
            cycle_start_ns);

        timing.active_cycles++;

        state_lock();

        /*
         * ----------------------------------------------------
         * Temporal freshness
         * ----------------------------------------------------
         */
        healink_staleness_update(
            &g_shared_state->data,
            current_time_ns);

        /*
         * ----------------------------------------------------
         * Sensor fusion
         * ----------------------------------------------------
         */
        healink_fusion_update(
            &g_shared_state->data);

        /*
         * ----------------------------------------------------
         * Health/activity classification
         * ----------------------------------------------------
         */
        healink_classifier_update(
            &g_shared_state->data);

        /* Keep a fall visible long enough for the fusion and safety paths to consume it. */
        if (g_shared_state->data.fall_event &&
            g_shared_state->data.last_fall_ts_ns != 0ULL &&
            current_time_ns >= g_shared_state->data.last_fall_ts_ns &&
            (current_time_ns - g_shared_state->data.last_fall_ts_ns) > HEALINK_FALL_LATCH_NS) {
            g_shared_state->data.fall_event = false;
        }

        {
            uint32_t safety_actions =
                healink_safety_update(&g_shared_state->data);

            snapshot = g_shared_state->data;

            state_unlock();

            if ((safety_actions & HEALINK_SAFETY_ACTION_ALERT) != 0U) {
                publish_event(
                    EVENT_ALERT,
                    snapshot.valid_mask,
                    snapshot.anomaly_score,
                    snapshot.overall_confidence);
            }
            if ((safety_actions & HEALINK_SAFETY_ACTION_DISTRESS) != 0U) {
                publish_event(
                    EVENT_DISTRESS,
                    snapshot.valid_mask,
                    snapshot.hr_bpm,
                    snapshot.spo2_pct);
            }
            if ((safety_actions & HEALINK_SAFETY_ACTION_EMERGENCY) != 0U) {
                publish_event(
                    EVENT_EMERGENCY,
                    snapshot.valid_mask,
                    snapshot.anomaly_score,
                    snapshot.fall_confidence);
            }
            if ((safety_actions & HEALINK_SAFETY_ACTION_RECOVERY) != 0U) {
                publish_event(
                    EVENT_RECOVERY,
                    snapshot.valid_mask,
                    snapshot.overall_confidence,
                    0.0f);
            }
        }

        /*
         * ----------------------------------------------------
         * STALE transition detection
         * ----------------------------------------------------
         *
         * Only newly stale sensors generate an event.
         *
         * If a sensor remains stale:
         *
         *     previous = STALE
         *     current  = STALE
         *
         * then:
         *
         *     newly_stale = 0
         *
         * Therefore the event queue is not flooded.
         */
        {
            uint32_t newly_stale =
                snapshot.stale_mask &
                ~previous_stale_mask;

            if (newly_stale != 0U) {

                publish_event(
                    EVENT_SENSOR_STALE,
                    newly_stale,
                    0.0f,
                    0.0f);
            }

            previous_stale_mask =
                snapshot.stale_mask;
        }

        /*
         * ----------------------------------------------------
         * Health state transition
         * ----------------------------------------------------
         */
        if (snapshot.health_state != previous_state) {

            publish_event(
                EVENT_STATE_CHANGE,
                snapshot.valid_mask,
                (float)snapshot.health_state,
                snapshot.overall_confidence);

            previous_state =
                snapshot.health_state;
        }

        /*
         * ----------------------------------------------------
         * ECG / PPG inconsistency
         * ----------------------------------------------------
         */
        if (snapshot.inconsistent_mask != 0U) {

            publish_event(
                EVENT_INCONSISTENT,
                snapshot.inconsistent_mask,
                snapshot.ecg_ppg_delta_bpm,
                snapshot.ecg_ppg_confidence);
        }

        {
            uint64_t cycle_end_ns = healink_now_ns();
            int sleep_rc;

            healink_timing_end(
                &timing,
                cycle_start_ns,
                cycle_end_ns,
                HEALINK_PERIOD_FUSION_NS);

            {
                uint64_t execution_ns =
                    (cycle_end_ns >= cycle_start_ns)
                        ? (cycle_end_ns - cycle_start_ns)
                        : 0ULL;

                timing.active_total_execution_ns +=
                    execution_ns;

                if (execution_ns >
                    timing.active_max_execution_ns) {
                    timing.active_max_execution_ns =
                        execution_ns;
                }
            }

            sleep_rc = healink_sleep_until(
                &next_wakeup_ns,
                HEALINK_PERIOD_FUSION_NS);

            if (sleep_rc > 0) {
                timing.schedule_skips += 1ULL;
            }

            if ((timing.cycles % 100ULL) == 0ULL &&
                timing.cycles != 0ULL) {
                print_timing_stats(
                    "FUSION",
                    &timing);
            }
        }
    }

    return NULL;
}


static void restore_terminal(void)
{
    if (g_terminal_raw_enabled) {
        (void)tcsetattr(
            STDIN_FILENO,
            TCSANOW,
            &g_terminal_saved);
        g_terminal_raw_enabled = false;
    }
}

static void enable_terminal_keys(void)
{
    struct termios terminal_settings;

    if (!isatty(STDIN_FILENO)) {
        return;
    }

    if (tcgetattr(STDIN_FILENO, &g_terminal_saved) != 0) {
        return;
    }

    (void)tcflush(STDIN_FILENO, TCIFLUSH);

    terminal_settings = g_terminal_saved;
    terminal_settings.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    terminal_settings.c_cc[VMIN] = 0;
    terminal_settings.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal_settings) == 0) {
        g_terminal_raw_enabled = true;
    }
}

static void poll_ui_keys(healink_ui_t *ui)
{
    fd_set read_set;
    struct timeval timeout;
    char ch;

    if (!g_terminal_raw_enabled) {
        return;
    }

    FD_ZERO(&read_set);
    FD_SET(STDIN_FILENO, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    if (select(STDIN_FILENO + 1,
               &read_set,
               NULL,
               NULL,
               &timeout) > 0 &&
        FD_ISSET(STDIN_FILENO, &read_set)) {
        while (read(STDIN_FILENO, &ch, 1) == 1) {
            if (ch == 'd' || ch == 'D') {
                if (ui != NULL) {
                    healink_ui_replay_demo(ui, healink_now_ns());
                    printf("\n[HEALINK] Full cinematic DEMO replay\n");
                }
            } else if (ch == 's' || ch == 'S') {
                publish_event(
                    EVENT_SOS,
                    0U,
                    0.0f,
                    0.0f);

                printf(
                    "\n[HEALINK] KEYBOARD SOS | S -> EVENT_SOS\n");
            }
        }
    }
}

/*
 * ============================================================
 * PHYSICAL SOS BUTTON THREAD
 * ============================================================
 *
 * BCM GPIO24 is used as an active-low momentary SOS button.
 * The input is sampled at 10 ms and requires five identical
 * samples before a transition is accepted, providing a simple
 * deterministic 50 ms debounce filter.
 */
static void *sos_button_thread(void *thread_arg)
{
    int last_raw_level = 1;
    int stable_level = 1;
    unsigned stable_sample_count = 0U;
    bool button_initialized = false;

    (void)thread_arg;

    while (g_running) {
        int raw_level = 1;

        if (rpi_gpio_read_level(
                HEALINK_GPIO_SOS_BUTTON,
                &raw_level) == 0) {

            if (!button_initialized) {
                /* Establish the real startup level without generating an event. */
                last_raw_level = raw_level;
                stable_level = raw_level;
                stable_sample_count = 1U;
                button_initialized = true;
            } else if (raw_level == last_raw_level) {
                if (stable_sample_count < 5U) {
                    ++stable_sample_count;
                }
            } else {
                last_raw_level = raw_level;
                stable_sample_count = 1U;
            }

            if (button_initialized &&
                stable_sample_count >= 5U &&
                raw_level != stable_level) {
                const int previous_stable_level = stable_level;
                stable_level = raw_level;

                /* SOS is generated only on a real HIGH->LOW transition after startup. */
                if (previous_stable_level == 1 && stable_level == 0) {
                    publish_event(
                        EVENT_SOS,
                        0U,
                        0.0f,
                        0.0f);

                    printf(
                        "[HEALINK] PHYSICAL SOS BUTTON PRESSED | "
                        "GPIO%d -> EVENT_SOS\n",
                        HEALINK_GPIO_SOS_BUTTON);
                } else if (previous_stable_level == 0 && stable_level == 1) {
                    printf(
                        "[HEALINK] SOS button released | GPIO%d\n",
                        HEALINK_GPIO_SOS_BUTTON);
                }
            }
        }

        {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 10000000L;
            (void)nanosleep(&ts, NULL);
        }
    }

    return NULL;
}


static void *ui_thread(void *thread_arg)
{
    uint64_t next_wakeup_ns = healink_now_ns();
    bool ui_started = false;

    (void)thread_arg;

    printf("\n");
    printf("============================================================\n");
    printf("                         HEALINK\n");
    printf("             QNX WEARABLE HEALTH FUSION CORE\n");
    printf("============================================================\n");
    printf("REAL HARDWARE MODE - NO SYNTHETIC SENSOR DATA\n\n");

    /* Keyboard backup is useful even when the physical GPIO button is unavailable. */
    enable_terminal_keys();

    if (g_oled_ready) {
        if (healink_ui_init(
                &g_user_interface,
                &g_oled_display,
                next_wakeup_ns,
                g_demo_start_enabled) == 0) {
            ui_started = true;
            printf(
                "[HEALINK] OLED intro: %s mode\n",
                g_demo_start_enabled ? "FULL DEMO" : "FAST 6-SECOND");
            printf(
                "[HEALINK] Manual SOS: GPIO24 or keyboard S. D replays demo.\n");
        } else {
            printf("[HEALINK] OLED UI initialization failed\n");
        }
    }

    while (g_running) {
        healink_sensor_state_t sensor_snapshot;

        state_lock();
        sensor_snapshot = g_shared_state->data;
        state_unlock();

        {
            char hr_text[24];
            char spo2_text[24];
            char temp_text[24];
            char accel_text[24];

            if ((sensor_snapshot.valid_mask & (SENSOR_MAX30102 | SENSOR_AD8232)) != 0U) {
                (void)snprintf(hr_text, sizeof(hr_text), "%.1f", sensor_snapshot.hr_bpm);
            } else {
                (void)snprintf(hr_text, sizeof(hr_text), "--");
            }

            if ((sensor_snapshot.valid_mask & SENSOR_MAX30102) != 0U) {
                (void)snprintf(spo2_text, sizeof(spo2_text), "%.1f", sensor_snapshot.spo2_pct);
            } else {
                (void)snprintf(spo2_text, sizeof(spo2_text), "--");
            }

            if ((sensor_snapshot.valid_mask & SENSOR_DS18B20) != 0U) {
                (void)snprintf(temp_text, sizeof(temp_text), "%.2f", sensor_snapshot.temperature_c);
            } else {
                (void)snprintf(temp_text, sizeof(temp_text), "--");
            }

            if ((sensor_snapshot.valid_mask & SENSOR_MPU6050) != 0U) {
                (void)snprintf(accel_text, sizeof(accel_text), "%.2f", sensor_snapshot.accel_mag_g);
            } else {
                (void)snprintf(accel_text, sizeof(accel_text), "--");
            }

            printf(
                "HR %sensor_snapshot | SpO2 %sensor_snapshot | Temp %sensor_snapshot | Accel %sG | "
                "Conf %.2f | %sensor_snapshot\n",
                hr_text,
                spo2_text,
                temp_text,
                accel_text,
                sensor_snapshot.overall_confidence,
                healink_health_state_str(sensor_snapshot.health_state));

                    }

        printf(
            "VALID=0x%08X STALE=0x%08X OFFLINE=0x%08X "
            "FAULT=0x%08X INCONSISTENT=0x%08X ACTIVITY=%sensor_snapshot\n",
            sensor_snapshot.valid_mask,
            sensor_snapshot.stale_mask,
            sensor_snapshot.offline_mask,
            sensor_snapshot.fault_mask,
            sensor_snapshot.inconsistent_mask,
            healink_activity_str(sensor_snapshot.activity));

        printf(
            "QUALITY ECG=%.2f PPG=%.2f HR_STAB=%.2f "
            "ANOMALY=%.2f FALL_CONF=%.2f\n",
            sensor_snapshot.ecg_quality,
            sensor_snapshot.ppg_quality,
            sensor_snapshot.hr_stability,
            sensor_snapshot.anomaly_score,
            sensor_snapshot.fall_confidence);

        /*
         * OLED rendering is presentation-only and remains outside
         * the sensor/fusion deadline path.
         */
        /* Keyboard backup remains active even when OLED init failed. */
        poll_ui_keys(ui_started ? &g_user_interface : NULL);

        if (ui_started) {
            healink_ui_tick(
                &g_user_interface,
                &sensor_snapshot,
                healink_now_ns());
        }

        /*
         * 50 ms UI cadence gives smooth cinematic animation while
         * keeping OLED work isolated to this low-priority thread.
         */
        (void)healink_sleep_until(&next_wakeup_ns, 50000000ULL);
    }

    if (ui_started) {
        healink_ui_shutdown(&g_user_interface);
        restore_terminal();
    }

    return NULL;
}


static int start_thread(
    pthread_t *thread_id,
    void *(*thread_function)(void *),
    int priority,
    void *thread_arg)
{
    pthread_attr_t attr;
    struct sched_param param;
    int result;

    result = pthread_attr_init(&attr);

    if (result != 0) {
        return result;
    }

    result = pthread_attr_setinheritsched(
        &attr,
        PTHREAD_EXPLICIT_SCHED);

    if (result != 0) {
        (void)pthread_attr_destroy(&attr);
        return result;
    }

    result = pthread_attr_setschedpolicy(
        &attr,
        SCHED_FIFO);

    if (result != 0) {
        (void)pthread_attr_destroy(&attr);
        return result;
    }

    memset(
        &param,
        0,
        sizeof(param));

    param.sched_priority =
        priority;

    result = pthread_attr_setschedparam(
        &attr,
        &param);

    if (result != 0) {
        (void)pthread_attr_destroy(&attr);
        return result;
    }

    result = pthread_create(
        thread_id,
        &attr,
        thread_function,
        thread_arg);

    (void)pthread_attr_destroy(&attr);

    return result;
}


static void log_init_failure(
    const char *device_name)
{
    printf(
        "[HEALINK] %-10s OFFLINE "
        "(hardware not present or backend unavailable)\n",
        device_name);
}


static void join_thread(
    pthread_t thread_id,
    bool *thread_started)
{
    if (thread_started != NULL &&
        *thread_started) {

        (void)pthread_join(
            thread_id,
            NULL);

        *thread_started = false;
    }
}


int main(int argc, char *argv[])
{
    int result;
    struct sigaction sa;

    if (argc > 2 ||
        (argc == 2 && strcmp(argv[1], "--demo") != 0)) {
        fprintf(
            stderr,
            "Usage: %s [--demo]\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 2 && strcmp(argv[1], "--demo") == 0) {
        g_demo_start_enabled = true;
    }

    memset(
        &sa,
        0,
        sizeof(sa));

    sa.sa_handler =
        on_signal;

    if (sigemptyset(
            &sa.sa_mask) != 0) {

        perror(
            "sigemptyset");

        return EXIT_FAILURE;
    }

    (void)sigaction(
        SIGINT,
        &sa,
        NULL);

    (void)sigaction(
        SIGTERM,
        &sa,
        NULL);

    /* SHARED STATE */

    if (healink_state_create(
            &g_shared_state,
            &g_shared_memory_fd) != 0) {

        perror(
            "healink_state_create");

        return EXIT_FAILURE;
    }

    /* EVENT QUEUE */

    if (healink_event_queue_open(
            &g_event_queue,
            1) != 0) {

        perror(
            "healink_event_queue_open");

        healink_state_destroy(
            g_shared_state,
            g_shared_memory_fd,
            1);

        return EXIT_FAILURE;
    }

    printf(
        "[HEALINK] Initializing QNX hardware interfaces...\n");

    /* QNX I2C */

    result = healink_i2c_open(
        &g_i2c_bus,
        HEALINK_I2C_DEV,
        400000U);

    if (result == 0) {

        g_i2c_ready = true;

        /*
         * ----------------------------------------------------
         * MAX30102
         * ----------------------------------------------------
         */

        if (max30102_init(
                &g_max30102_sensor,
                &g_i2c_bus,
                HEALINK_MAX30102_ADDR) == 0) {

            g_max30102_ready = true;

            mark_sensor_online(
                SENSOR_MAX30102);

            printf(
                "[HEALINK] MAX30102  OK\n");

        } else {

            mark_sensor_offline(
                SENSOR_MAX30102);

            log_init_failure(
                "MAX30102");
        }

        /*
         * ----------------------------------------------------
         * ADS1115
         * ----------------------------------------------------
         *
         * ADS1115 is an acquisition dependency for AD8232.
         * It does not have its own health-state bit.
         */

        if (ads1115_init(
                &g_ads1115_adc,
                &g_i2c_bus,
                HEALINK_ADS1115_ADDR) == 0) {

            g_ads1115_ready = true;

            printf(
                "[HEALINK] ADS1115   OK\n");

        } else {

            log_init_failure(
                "ADS1115");
        }

        /*
         * ----------------------------------------------------
         * AD8232
         * ----------------------------------------------------
         */

        if (g_ads1115_ready &&
            ad8232_init(
                &g_ad8232_ecg,
                &g_ads1115_adc) == 0) {

            g_ad8232_ready = true;

            mark_sensor_online(
                SENSOR_AD8232);

            if (g_ad8232_ecg.lead_status_available) {
                printf(
                    "[HEALINK] AD8232    READY; lead-off GPIO status ACTIVE\n");
            } else {
                printf(
                    "[HEALINK] AD8232    READY; lead-off GPIO status UNAVAILABLE\n");
            }

        } else {

            mark_sensor_offline(
                SENSOR_AD8232);

            log_init_failure(
                "AD8232");
        }

        /*
         * ----------------------------------------------------
         * MPU6050
         * ----------------------------------------------------
         */

        if (mpu6050_init(
                &g_mpu6050_sensor,
                &g_i2c_bus,
                HEALINK_MPU6050_ADDR) == 0) {

            g_mpu6050_ready = true;

            if (g_mpu6050_sensor.who_am_i == 0x70U) {
                printf(
                    "[HEALINK] MPU6050   ONLINE; WHO_AM_I=0x70 (MPU6500-family compatible)\n");
            } else {
                printf(
                    "[HEALINK] MPU6050   ONLINE; WHO_AM_I=0x68 (MPU6050)\n");
            }

            if (rpi_gpio_set_input(HEALINK_GPIO_MPU6050_INT) != 0) {
                printf(
                    "[HEALINK] MPU6050   GPIO INT unavailable (polling remains active)\n");
            } else {
                printf(
                    "[HEALINK] MPU6050   GPIO%d INPUT configured (interrupt path reserved)\n",
                    HEALINK_GPIO_MPU6050_INT);
            }

            mark_sensor_online(
                SENSOR_MPU6050);

        } else {

            mark_sensor_offline(
                SENSOR_MPU6050);

            log_init_failure(
                "MPU6050");
        }

        /*
         * ----------------------------------------------------
         * SSD1306
         * ----------------------------------------------------
         */

        if (ssd1306_init(
                &g_oled_display,
                &g_i2c_bus,
                HEALINK_OLED_I2C_ADDR) == 0) {

            g_oled_ready = true;

            printf(
                "[HEALINK] SSD1306   OK\n");

        } else {

            log_init_failure(
                "SSD1306");
        }

    } else {

        /*
         * I2C itself is unavailable.
         *
         * All I2C health sensors therefore begin OFFLINE.
         *
         * This is not a runtime sensor fault because the
         * devices were never successfully initialized.
         */

        if (result > 0) {
            errno = result;
        } else {
            errno = -result;
        }

        perror(
            "[HEALINK] I2C open");

        mark_sensor_offline(
            SENSOR_MAX30102 |
            SENSOR_AD8232 |
            SENSOR_MPU6050);
    }

    /*
     * ========================================================
     * DS18B20
     * ========================================================
     *
     * Real 1-Wire presence detection is performed during init.
     * No synthetic temperature data is generated.
     */
    {
        int ds_rc =
            ds18b20_init(
                &g_ds18b20_sensor,
                HEALINK_GPIO_DS18B20);

        if (ds_rc == DS18B20_OK) {
            g_ds18b20_ready = true;

            mark_sensor_online(
                SENSOR_DS18B20);

            printf(
                "[HEALINK] DS18B20   ONLINE on GPIO%d\n",
                HEALINK_GPIO_DS18B20);

        } else {
            g_ds18b20_ready = false;

            mark_sensor_offline(
                SENSOR_DS18B20);

            if (ds_rc == DS18B20_NO_SENSOR) {
                printf(
                    "[HEALINK] DS18B20   OFFLINE "
                    "(no sensor on GPIO%d)\n",
                    HEALINK_GPIO_DS18B20);
            } else {
                log_init_failure(
                    "DS18B20");
            }
        }
    }

    /*
     * ========================================================
     * DHT11
     * ========================================================
     *
     * Ambient environment sensor. It is intentionally kept out of
     * SENSOR_ALL_HEALTH so it cannot manufacture physiological
     * confidence.
     */
    if (dht11_init(
            &g_dht11_sensor,
            HEALINK_GPIO_DHT11) == DHT11_OK) {

        g_dht11_ready = true;

        mark_sensor_online(SENSOR_DHT11);

        printf(
            "[HEALINK] DHT11     ONLINE on GPIO%d\n",
            HEALINK_GPIO_DHT11);

    } else {

        g_dht11_ready = false;

        mark_sensor_offline(SENSOR_DHT11);

        log_init_failure("DHT11");
    }

    /* WORKER THREADS */

    result = start_thread(
        &g_max30102_thread,
        max30102_thread,
        HEALINK_PRIO_MAX30102,
        NULL);

    if (result != 0) {

        errno = result;

        perror(
            "pthread_create MAX30102");

        goto shutdown;
    }

    g_max30102_thread_started = true;

    result = start_thread(
        &g_ad8232_thread,
        ad8232_thread,
        HEALINK_PRIO_AD8232,
        NULL);

    if (result != 0) {

        errno = result;

        perror(
            "pthread_create AD8232");

        goto shutdown;
    }

    g_ad8232_thread_started = true;

    result = start_thread(
        &g_mpu6050_thread,
        mpu6050_thread,
        HEALINK_PRIO_MPU6050,
        NULL);

    if (result != 0) {

        errno = result;

        perror(
            "pthread_create MPU6050");

        goto shutdown;
    }

    g_mpu6050_thread_started = true;

    result = start_thread(
        &g_ds18b20_thread,
        ds18b20_thread,
        HEALINK_PRIO_DS18B20,
        NULL);

    if (result != 0) {

        errno = result;

        perror(
            "pthread_create DS18B20");

        goto shutdown;
    }

    g_ds18b20_thread_started = true;

    result = start_thread(
        &g_dht11_thread,
        dht11_thread,
        HEALINK_PRIO_DHT11,
        NULL);

    if (result != 0) {

        errno = result;
        perror("[HEALINK] pthread_create DHT11");

    } else {

        g_dht11_thread_started = true;
    }

    result = start_thread(
        &g_fusion_thread,
        fusion_thread,
        HEALINK_PRIO_FUSION,
        NULL);

    if (result != 0) {

        errno = result;

        perror(
            "pthread_create FUSION");

        goto shutdown;
    }

    g_fusion_thread_started = true;

    result = start_thread(
        &g_ui_thread,
        ui_thread,
        HEALINK_PRIO_UI,
        NULL);

    if (result != 0) {

        errno = result;

        perror(
            "pthread_create UI");

        goto shutdown;
    }

    g_ui_thread_started = true;

    /*
     * ========================================================
     * PHYSICAL SOS BUTTON
     * ========================================================
     *
     * BCM GPIO24 is dedicated to the QNX-side LoRa #1 SOS button.
     * The input is active LOW and uses an external 10 kOhm pull-up
     * to 3.3 V with the momentary push-button connected to GND.
     */
    if (rpi_gpio_set_input(HEALINK_GPIO_SOS_BUTTON) == 0) {
        printf(
            "[HEALINK] SOS BUTTON READY | GPIO%d | active LOW\n",
            HEALINK_GPIO_SOS_BUTTON);

        result = start_thread(
            &g_sos_button_thread,
            sos_button_thread,
            HEALINK_PRIO_SOS_BUTTON,
            NULL);

        if (result != 0) {
            errno = result;
            perror("[HEALINK] pthread_create SOS BUTTON");
        } else {
            g_sos_button_thread_started = true;
        }
    } else {
        printf(
            "[HEALINK] SOS BUTTON OFFLINE | GPIO%d could not be configured\n",
            HEALINK_GPIO_SOS_BUTTON);
    }

    /*
     * ========================================================
     * QNX LoRa COMMUNICATION WORKER
     * ========================================================
     *
     * LoRa #1 is physically attached to the Raspberry Pi.  The
     * worker owns the SX1278 SPI handle; the emergency thread
     * submits requests through the dedicated POSIX message queue.
     */
    if (healink_lora_init(
            &g_lora_context,
            &g_running,
            g_event_queue) == 0) {

        g_lora_ready = true;

        result = start_thread(
            &g_lora_thread,
            healink_lora_thread,
            HEALINK_PRIO_LORA,
            &g_lora_context);

        if (result != 0) {
            errno = result;
            perror("[HEALINK] pthread_create LORA");
            healink_lora_shutdown(&g_lora_context);
            g_lora_ready = false;
        } else {
            g_lora_thread_started = true;
            printf(
                "[HEALINK] LoRa #1 ONLINE | SX1278 /dev/io-spi/spi0/dev0 | 433 MHz\n");
        }
    } else {
        printf(
            "[HEALINK] LoRa #1 OFFLINE | SX1278 SPI/identity unavailable\n");
    }

    g_emergency_context.mq_ptr = &g_event_queue;
    g_emergency_context.running_ptr = &g_running;
    g_emergency_context.sos_callback =
        g_lora_ready ? healink_lora_request_sos : NULL;
    g_emergency_context.sos_callback_ctx =
        g_lora_ready ? (void *)&g_lora_context : NULL;
    g_emergency_context.event_callback =
        g_oled_ready ? notify_ui_event : NULL;
    g_emergency_context.event_callback_ctx =
        g_oled_ready ? (void *)&g_user_interface : NULL;

    result = start_thread(
        &g_emergency_thread,
        healink_emergency_thread,
        HEALINK_PRIO_EMERGENCY,
        &g_emergency_context);

    if (result != 0) {

        errno = result;

        perror(
            "pthread_create EMERGENCY");

        goto shutdown;
    }

    g_emergency_thread_started = true;

    /* CORE STARTED */

    printf("\n");

    printf(
        "[HEALINK] CORE STARTED\n");

    printf(
        "[HEALINK] Physical SOS button: GPIO%d | Remote ACK: ESP32 GPIO27\n",
        HEALINK_GPIO_SOS_BUTTON);

    printf(
        "[HEALINK] No simulated data is generated. "
        "Missing hardware stays OFFLINE; "
        "missed updates become STALE.\n");

    printf(
        "[HEALINK] Press Ctrl+C to stop.\n\n");

    while (g_running) {

        struct timespec ts;

        ts.tv_sec = 1;
        ts.tv_nsec = 0;

        (void)nanosleep(
            &ts,
            NULL);
    }

shutdown:

    /* SHUTDOWN */

    g_running = 0;

    /*
     * Best-effort shutdown event for deterministic diagnostics.
     * The emergency worker no longer depends on this message to
     * terminate: it observes g_running and wakes periodically.
     */
    if (g_event_queue != (mqd_t)-1 &&
        g_emergency_thread_started) {

        publish_event(
            EVENT_SHUTDOWN,
            0U,
            0.0f,
            0.0f);
    }

    /*
     * Join all created worker threads before destroying the
     * shared state and event queue.
     */
    join_thread(
        g_max30102_thread,
        &g_max30102_thread_started);

    join_thread(
        g_ad8232_thread,
        &g_ad8232_thread_started);

    join_thread(
        g_mpu6050_thread,
        &g_mpu6050_thread_started);

    join_thread(
        g_ds18b20_thread,
        &g_ds18b20_thread_started);

    join_thread(
        g_fusion_thread,
        &g_fusion_thread_started);

    join_thread(
        g_ui_thread,
        &g_ui_thread_started);

    join_thread(
        g_sos_button_thread,
        &g_sos_button_thread_started);

    join_thread(
        g_lora_thread,
        &g_lora_thread_started);

    join_thread(
        g_emergency_thread,
        &g_emergency_thread_started);

    if (g_lora_ready) {
        healink_lora_shutdown(&g_lora_context);
        g_lora_ready = false;
    }

    if (g_dht11_ready) {
        dht11_close(&g_dht11_sensor);
        g_dht11_ready = false;
    }

    /* OLED CLEANUP */

    if (g_oled_ready) {

        ssd1306_display_off(
            &g_oled_display);

        g_oled_ready = false;
    }

    /* I2C CLEANUP */

    if (g_i2c_ready) {

        healink_i2c_close(
            &g_i2c_bus);

        g_i2c_ready = false;
    }

    /* EVENT QUEUE CLEANUP */

    if (g_event_queue != (mqd_t)-1) {

        healink_event_queue_close(
            g_event_queue,
            1);

        g_event_queue =
            (mqd_t)-1;
    }

    /* SHARED MEMORY CLEANUP */

    if (g_shared_state != NULL) {

        healink_state_destroy(
            g_shared_state,
            g_shared_memory_fd,
            1);

        g_shared_state = NULL;
        g_shared_memory_fd = -1;
    }

    printf(
        "\n[HEALINK] OFFLINE\n");

    restore_terminal();

    return EXIT_SUCCESS;
}
