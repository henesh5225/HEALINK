#define _POSIX_C_SOURCE 200809L

#include "healink_staleness.h"

#include <stddef.h>
#include <stdint.h>

#include "healink_config.h"

/*
 * ============================================================
 * CHECK ONE SENSOR
 * ============================================================
 *
 * Staleness is purely temporal.
 *
 * It does NOT determine whether a sensor is:
 *
 *     OFFLINE
 *     FAULT
 *     VALID
 *
 * Those states are maintained by the sensor/runtime layer.
 *
 * A timestamp of zero means that the sensor has never produced
 * a sample, therefore its data is stale.
 */

static void check_sensor_freshness(
    uint64_t now_ns,
    uint64_t timestamp_ns,
    uint64_t period_ns,
    uint32_t sensor_bit,
    uint32_t *stale_mask,
    uint32_t offline_mask,
    uint32_t fault_mask,
    uint32_t *valid_mask)
{
    uint64_t stale_limit_ns;

    if (stale_mask == NULL || valid_mask == NULL) {
        return;
    }

    /*
     * OFFLINE and FAULT are lifecycle states. They must not also
     * be reported as STALE, otherwise one sensor appears to be in
     * multiple mutually exclusive lifecycle states.
     */
    if ((offline_mask & sensor_bit) != 0U ||
        (fault_mask & sensor_bit) != 0U) {
        *stale_mask &= ~sensor_bit;
        *valid_mask &= ~sensor_bit;
        return;
    }

    /*
     * Calculate:
     *
     *     expected period × stale margin
     *
     * Current configuration:
     *
     *     3 / 2 = 1.5 × expected period
     */
    stale_limit_ns =
        (period_ns * HEALINK_STALE_MARGIN_NUM) /
        HEALINK_STALE_MARGIN_DEN;

    /*
     * No sample has ever been received.
     */
    if (timestamp_ns == 0ULL) {
        *stale_mask |= sensor_bit;
        *valid_mask &= ~sensor_bit;
        return;
    }

    /*
     * A future timestamp cannot represent a valid elapsed
     * interval on the same monotonic clock.
     */
    if (now_ns < timestamp_ns) {
        *stale_mask |= sensor_bit;
        *valid_mask &= ~sensor_bit;
        return;
    }

    /*
     * Sensor has missed its freshness deadline.
     */
    if ((now_ns - timestamp_ns) > stale_limit_ns) {
        *stale_mask |= sensor_bit;
        *valid_mask &= ~sensor_bit;
    }
}

/* UPDATE STALENESS */

void healink_staleness_update(
    healink_sensor_state_t *state,
    uint64_t now_ns)
{
    if (state == NULL) {
        return;
    }

    /*
     * Recalculate the temporal state from scratch on every
     * fusion cycle.
     *
     * The staleness layer owns stale_mask and removes VALID when
     * freshness is lost. It never creates or clears OFFLINE/FAULT.
     *
     * The following masks deliberately remain untouched:
     *
     *     valid_mask
     *     offline_mask
     *     fault_mask
     *     inconsistent_mask
     *
     * Keeping these dimensions independent is important because
     * "offline", "fault", "stale", and "inconsistent" describe
     * different failure/quality conditions.
     */

    state->stale_mask = 0U;

    /*
     * MAX30102:
     *
     * 100 Hz nominal period.
     */
    check_sensor_freshness(
        now_ns,
        state->max30102_ts_ns,
        HEALINK_PERIOD_MAX30102_NS,
        SENSOR_MAX30102,
        &state->stale_mask,
        state->offline_mask,
        state->fault_mask,
        &state->valid_mask);

    /*
     * AD8232:
     *
     * 250 Hz nominal acquisition period.
     */
    check_sensor_freshness(
        now_ns,
        state->ad8232_ts_ns,
        HEALINK_PERIOD_AD8232_NS,
        SENSOR_AD8232,
        &state->stale_mask,
        state->offline_mask,
        state->fault_mask,
        &state->valid_mask);

    /*
     * MPU6050:
     *
     * 50 Hz nominal period.
     */
    check_sensor_freshness(
        now_ns,
        state->mpu6050_ts_ns,
        HEALINK_PERIOD_MPU6050_NS,
        SENSOR_MPU6050,
        &state->stale_mask,
        state->offline_mask,
        state->fault_mask,
        &state->valid_mask);

    /*
     * DS18B20:
     *
     * 1 Hz nominal period.
     */
    check_sensor_freshness(
        now_ns,
        state->ds18b20_ts_ns,
        HEALINK_PERIOD_DS18B20_NS,
        SENSOR_DS18B20,
        &state->stale_mask,
        state->offline_mask,
        state->fault_mask,
        &state->valid_mask);

    /*
     * DHT11: ambient temperature / humidity, 0.5 Hz nominal.
     * It is environmental context and intentionally not part of the
     * physiological coverage policy.
     */
    check_sensor_freshness(
        now_ns,
        state->dht11_ts_ns,
        HEALINK_PERIOD_DHT11_NS,
        SENSOR_DHT11,
        &state->stale_mask,
        state->offline_mask,
        state->fault_mask,
        &state->valid_mask);

}
