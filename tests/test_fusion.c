#include "healink_fusion.h"
#include "healink_classifier.h"
#include "healink_staleness.h"
#include "healink_safety.h"
#include "healink_config.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void reset_state(
    healink_sensor_state_t *state)
{
    memset(state, 0, sizeof(*state));

    state->health_state = HEALTH_UNKNOWN;
    state->activity = ACTIVITY_UNKNOWN;

    /* Optional environmental channels are intentionally absent from the
     * physiological unit-test fixture. Mark them OFFLINE so their zero
     * timestamps do not pollute health-sensor staleness assertions. */
    state->offline_mask = SENSOR_DHT11;
}

static void test_both_sources_agree(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    state.ecg_quality = 1.0f;
    state.ppg_quality = 1.0f;

    state.ecg_hr_bpm = 82.0f;
    state.ppg_hr_bpm = 80.0f;
    state.spo2_pct = 98.0f;
    state.temperature_c = 36.5f;
    state.accel_mag_g = 1.0f;

    state.valid_mask =
        SENSOR_MAX30102 |
        SENSOR_AD8232 |
        SENSOR_MPU6050 |
        SENSOR_DS18B20;

    state.max30102_ts_ns = 1045000000ULL;
    state.ad8232_ts_ns = 1047000000ULL;
    state.mpu6050_ts_ns = 1040000000ULL;
    state.ds18b20_ts_ns = 1000000000ULL;

    healink_staleness_update(
        &state,
        1050000000ULL);

    healink_fusion_update(&state);
    healink_classifier_update(&state);

    assert(state.stale_mask == 0U);
    assert(state.inconsistent_mask == 0U);

    /*
     * The important A2 assertion:
     * HR must actually be fused.
     */
    assert(fabsf(state.hr_bpm - 81.0f) < 0.01f);

    assert(fabsf(state.ecg_ppg_delta_bpm - 2.0f) < 0.01f);
    assert(fabsf(state.hr_confidence - 1.0f) < 0.01f);
    assert(fabsf(state.ecg_ppg_confidence - 1.0f) < 0.01f);

    assert(state.health_state == HEALTH_RESTING);
    assert(state.activity == ACTIVITY_RESTING);
}

static void test_ecg_only(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    state.ecg_quality = 1.0f;
    state.ppg_quality = 1.0f;

    state.ecg_hr_bpm = 76.0f;

    state.valid_mask =
        SENSOR_AD8232;

    state.ad8232_ts_ns = 1000000000ULL;

    healink_staleness_update(
        &state,
        1001000000ULL);

    healink_fusion_update(&state);
    healink_classifier_update(&state);

    assert(fabsf(state.hr_bpm - 76.0f) < 0.01f);
    assert(fabsf(state.hr_confidence - 0.65f) < 0.01f);
    assert(state.ecg_ppg_delta_bpm == 0.0f);
    assert(state.inconsistent_mask == 0U);
    assert(state.health_state == HEALTH_RESTING);
}

static void test_ppg_only(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    state.ecg_quality = 1.0f;
    state.ppg_quality = 1.0f;

    state.ppg_hr_bpm = 78.0f;
    state.spo2_pct = 98.0f;

    state.valid_mask =
        SENSOR_MAX30102;

    state.max30102_ts_ns = 1000000000ULL;

    healink_staleness_update(
        &state,
        1001000000ULL);

    healink_fusion_update(&state);
    healink_classifier_update(&state);

    assert(fabsf(state.hr_bpm - 78.0f) < 0.01f);
    assert(fabsf(state.hr_confidence - 0.65f) < 0.01f);
    assert(state.inconsistent_mask == 0U);
    assert(state.health_state == HEALTH_RESTING);
}

static void test_temperature_only_is_uncertain(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    state.temperature_c = 27.8f;
    state.valid_mask = SENSOR_DS18B20;
    state.ds18b20_ts_ns = 1000000000ULL;

    healink_staleness_update(
        &state,
        1001000000ULL);

    healink_fusion_update(&state);
    healink_classifier_update(&state);

    assert(fabsf(state.temperature_confidence - 1.0f) < 0.01f);
    assert(fabsf(state.overall_confidence - HEALINK_CONF_MAX_NO_PHYSIOLOGY) < 0.01f);
    assert(state.health_state == HEALTH_SENSOR_UNCERTAIN);
    assert(state.activity == ACTIVITY_UNKNOWN);
}

static void test_moderate_disagreement(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    state.ecg_quality = 1.0f;
    state.ppg_quality = 1.0f;

    state.ecg_hr_bpm = 90.0f;
    state.ppg_hr_bpm = 101.0f;

    state.valid_mask =
        SENSOR_MAX30102 |
        SENSOR_AD8232;

    state.max30102_ts_ns = 1000000000ULL;
    state.ad8232_ts_ns = 1000000000ULL;

    healink_staleness_update(
        &state,
        1001000000ULL);

    healink_fusion_update(&state);
    healink_classifier_update(&state);

    /*
     * 11 BPM is within the configured 15 BPM maximum but is
     * only weak agreement.
     */
    assert(fabsf(state.hr_bpm - 95.5f) < 0.01f);
    assert(fabsf(state.ecg_ppg_delta_bpm - 11.0f) < 0.01f);
    assert(fabsf(state.hr_confidence - 0.60f) < 0.01f);
    assert(state.inconsistent_mask == 0U);
}

static void test_strong_disagreement(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    state.ecg_quality = 1.0f;
    state.ppg_quality = 1.0f;

    state.ecg_hr_bpm = 140.0f;
    state.ppg_hr_bpm = 80.0f;

    state.valid_mask =
        SENSOR_MAX30102 |
        SENSOR_AD8232;

    state.max30102_ts_ns = 1000000000ULL;
    state.ad8232_ts_ns = 1000000000ULL;

    healink_staleness_update(
        &state,
        1001000000ULL);

    healink_fusion_update(&state);
    healink_classifier_update(&state);

    /*
     * The system must NOT average 140 and 80 into 110 BPM.
     */
    assert(fabsf(state.hr_bpm - 140.0f) < 0.01f);

    assert(fabsf(state.ecg_ppg_delta_bpm - 60.0f) < 0.01f);

    assert((state.inconsistent_mask &
            SENSOR_AD8232) != 0U);

    assert((state.inconsistent_mask &
            SENSOR_MAX30102) != 0U);

    assert(fabsf(state.hr_confidence - 0.20f) < 0.01f);
    assert(fabsf(state.ecg_ppg_confidence - 0.0f) < 0.01f);

    assert(state.health_state ==
           HEALTH_SENSOR_UNCERTAIN);
}

static void test_stale_source_excluded(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    state.ecg_quality = 1.0f;
    state.ppg_quality = 1.0f;

    state.ecg_hr_bpm = 75.0f;
    state.ppg_hr_bpm = 120.0f;

    state.valid_mask =
        SENSOR_MAX30102 |
        SENSOR_AD8232;

    /*
     * ECG is fresh.
     * PPG timestamp is deliberately stale.
     */
    state.ad8232_ts_ns = 2000000000ULL;
    state.max30102_ts_ns = 1000000000ULL;

    healink_staleness_update(
        &state,
        2000000000ULL);

    healink_fusion_update(&state);

    /*
     * Stale PPG must not participate in fusion.
     */
    assert((state.stale_mask &
            SENSOR_MAX30102) != 0U);

    assert(fabsf(state.hr_bpm - 75.0f) < 0.01f);
    assert(fabsf(state.hr_confidence - 0.65f) < 0.01f);

    assert(state.inconsistent_mask == 0U);
}

static void test_no_sources(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    healink_fusion_update(&state);
    healink_classifier_update(&state);

    assert(state.hr_bpm == 0.0f);
    assert(state.hr_confidence == 0.0f);
    assert(state.overall_confidence == 0.0f);

    assert(state.health_state ==
           HEALTH_SENSOR_UNCERTAIN);

    assert(state.activity ==
           ACTIVITY_UNKNOWN);
}

static void test_offline_is_not_stale(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    state.offline_mask = SENSOR_MAX30102 | SENSOR_AD8232;
    state.valid_mask = SENSOR_MAX30102 | SENSOR_AD8232;

    healink_staleness_update(
        &state,
        5000000000ULL);

    assert((state.stale_mask &
            (SENSOR_MAX30102 | SENSOR_AD8232)) == 0U);

    assert((state.valid_mask &
            (SENSOR_MAX30102 | SENSOR_AD8232)) == 0U);
}

static void test_stale_clears_valid(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    state.valid_mask = SENSOR_AD8232;
    state.ad8232_ts_ns = 1000000000ULL;

    healink_staleness_update(
        &state,
        2000000000ULL);

    assert((state.stale_mask & SENSOR_AD8232) != 0U);
    assert((state.valid_mask & SENSOR_AD8232) == 0U);

    state.ad8232_ts_ns = 1995000000ULL;

    healink_staleness_update(
        &state,
        2000000000ULL);

    assert((state.stale_mask & SENSOR_AD8232) == 0U);
}

static void test_fault_is_not_stale(void)
{
    healink_sensor_state_t state;

    reset_state(&state);

    state.fault_mask = SENSOR_MPU6050;
    state.valid_mask = SENSOR_MPU6050;
    state.mpu6050_ts_ns = 1000000000ULL;

    healink_staleness_update(
        &state,
        5000000000ULL);

    assert((state.stale_mask & SENSOR_MPU6050) == 0U);
    assert((state.valid_mask & SENSOR_MPU6050) == 0U);
}


static void test_quality_aware_agreement(void)
{
    healink_sensor_state_t state;
    reset_state(&state);
    state.ecg_hr_bpm = 82.0f;
    state.ppg_hr_bpm = 83.0f;
    state.ecg_quality = 1.0f;
    state.ppg_quality = 1.0f;
    state.valid_mask = SENSOR_AD8232 | SENSOR_MAX30102;
    state.ad8232_ts_ns = 1000000000ULL;
    state.max30102_ts_ns = 1000000000ULL;
    healink_staleness_update(&state, 1001000000ULL);
    healink_fusion_update(&state);
    healink_classifier_update(&state);
    assert(state.hr_confidence > 0.70f);
    assert(state.hr_stability > 0.0f);
    assert(state.anomaly_score < 0.40f);
}

static void test_low_ppg_quality_reduces_confidence(void)
{
    healink_sensor_state_t state;
    reset_state(&state);
    state.ecg_hr_bpm = 80.0f;
    state.ppg_hr_bpm = 81.0f;
    state.ecg_quality = 1.0f;
    state.ppg_quality = 0.10f;
    state.valid_mask = SENSOR_AD8232 | SENSOR_MAX30102;
    state.ad8232_ts_ns = 1000000000ULL;
    state.max30102_ts_ns = 1000000000ULL;
    healink_staleness_update(&state, 1001000000ULL);
    healink_fusion_update(&state);
    assert(state.ppg_quality < 0.5f);
    assert(state.hr_confidence < 0.70f);
    assert(state.hr_bpm > 0.0f);
}

static void test_motion_context_dampens_activity_anomaly(void)
{
    healink_sensor_state_t active_state;
    healink_sensor_state_t rest_state;
    reset_state(&active_state);
    reset_state(&rest_state);

    active_state.ecg_hr_bpm = 130.0f;
    active_state.ecg_quality = 1.0f;
    active_state.valid_mask = SENSOR_AD8232 | SENSOR_MPU6050;
    active_state.ad8232_ts_ns = 1000000000ULL;
    active_state.mpu6050_ts_ns = 1000000000ULL;
    active_state.accel_mag_g = 1.8f;

    rest_state = active_state;
    rest_state.accel_mag_g = 1.0f;

    healink_staleness_update(&active_state, 1001000000ULL);
    healink_staleness_update(&rest_state, 1001000000ULL);
    healink_fusion_update(&active_state);
    healink_fusion_update(&rest_state);

    assert(active_state.anomaly_score < rest_state.anomaly_score);
}

static void test_low_spo2_quality_affects_classifier(void)
{
    healink_sensor_state_t state;
    reset_state(&state);
    state.ppg_hr_bpm = 82.0f;
    state.spo2_pct = 90.0f;
    state.ppg_quality = 1.0f;
    state.valid_mask = SENSOR_MAX30102;
    state.max30102_ts_ns = 1000000000ULL;
    healink_staleness_update(&state, 1001000000ULL);
    healink_fusion_update(&state);
    healink_classifier_update(&state);
    assert(state.spo2_confidence > 0.0f);
    assert(state.anomaly_score > 0.0f);
    assert(state.health_state == HEALTH_RESTING || state.health_state == HEALTH_STRESSED || state.health_state == HEALTH_DISTRESS);
}

static void test_fall_flag_promotes_fall_state(void)
{
    healink_sensor_state_t state;
    reset_state(&state);
    state.fall_event = true;
    state.fall_confidence = 1.0f;
    healink_fusion_update(&state);
    healink_classifier_update(&state);
    assert(state.health_state == HEALTH_FALL);
    assert(state.activity == ACTIVITY_FALL);
}


static void test_emergency_escalation_after_confirmed_distress(void)
{
    healink_sensor_state_t state;
    uint32_t actions;
    reset_state(&state);
    state.health_state = HEALTH_DISTRESS;
    state.spo2_pct = 84.0f;
    state.spo2_confidence = 1.0f;
    state.overall_confidence = 1.0f;

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_DISTRESS) != 0U);
    assert((actions & HEALINK_SAFETY_ACTION_EMERGENCY) == 0U);

    (void)healink_safety_update(&state);
    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_EMERGENCY) != 0U);
    assert(state.emergency_latched);

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_EMERGENCY) == 0U);
}

static void test_fall_causes_immediate_emergency_action(void)
{
    healink_sensor_state_t state;
    uint32_t actions;
    reset_state(&state);
    state.fall_event = true;
    state.fall_confidence = 1.0f;
    state.health_state = HEALTH_FALL;

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_EMERGENCY) != 0U);
    assert(state.emergency_latched);

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_EMERGENCY) == 0U);
}


static void test_transient_severe_distress_requires_confirmation(void)
{
    healink_sensor_state_t state;
    uint32_t actions;

    reset_state(&state);
    state.health_state = HEALTH_DISTRESS;
    state.spo2_pct = 84.0f;
    state.spo2_confidence = 1.0f;
    state.overall_confidence = 1.0f;

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_DISTRESS) != 0U);
    assert((actions & HEALINK_SAFETY_ACTION_EMERGENCY) == 0U);
    assert(state.distress_confirm_count == 1U);

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_EMERGENCY) == 0U);
    assert(state.distress_confirm_count == 2U);
    assert(!state.emergency_latched);

    state.health_state = HEALTH_RESTING;
    state.spo2_confidence = 1.0f;
    state.spo2_pct = 98.0f;

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_EMERGENCY) == 0U);
    assert(state.recovery_confirm_count == 1U);
    assert(!state.emergency_latched);
}

static void test_recovery_clears_latches_only_after_confirmation(void)
{
    healink_sensor_state_t state;
    uint32_t actions;
    unsigned i;

    reset_state(&state);
    state.health_state = HEALTH_STRESSED;

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_ALERT) != 0U);
    assert(state.alert_latched);

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_ALERT) == 0U);
    assert(state.alert_latched);

    state.health_state = HEALTH_RESTING;

    for (i = 1U; i < HEALINK_RECOVERY_CONFIRM_CYCLES; ++i) {
        actions = healink_safety_update(&state);
        assert((actions & HEALINK_SAFETY_ACTION_RECOVERY) == 0U);
        assert(state.alert_latched);
        assert(!state.emergency_latched);
    }

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_RECOVERY) != 0U);
    assert(!state.alert_latched);
    assert(!state.emergency_latched);
    assert(state.recovery_confirm_count == 0U);
}

static void test_recovery_window_resets_when_alarm_returns(void)
{
    healink_sensor_state_t state;
    uint32_t actions;

    reset_state(&state);
    state.health_state = HEALTH_STRESSED;
    (void)healink_safety_update(&state);
    assert(state.alert_latched);

    state.health_state = HEALTH_RESTING;
    (void)healink_safety_update(&state);
    assert(state.recovery_confirm_count == 1U);

    state.health_state = HEALTH_STRESSED;
    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_ALERT) == 0U);
    assert(state.recovery_confirm_count == 0U);
    assert(state.alert_latched);

    state.health_state = HEALTH_RESTING;
    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_RECOVERY) == 0U);
    assert(state.recovery_confirm_count == 1U);
}

static void test_fall_latch_recovers_after_fall_clears(void)
{
    healink_sensor_state_t state;
    uint32_t actions;
    unsigned i;

    reset_state(&state);
    state.health_state = HEALTH_FALL;
    state.fall_event = true;
    state.fall_confidence = 1.0f;

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_EMERGENCY) != 0U);
    assert(state.emergency_latched);

    state.fall_event = false;
    state.health_state = HEALTH_RESTING;

    for (i = 1U; i < HEALINK_RECOVERY_CONFIRM_CYCLES; ++i) {
        actions = healink_safety_update(&state);
        assert((actions & HEALINK_SAFETY_ACTION_RECOVERY) == 0U);
        assert(state.emergency_latched);
    }

    actions = healink_safety_update(&state);
    assert((actions & HEALINK_SAFETY_ACTION_RECOVERY) != 0U);
    assert(!state.emergency_latched);
    assert(!state.alert_latched);
}

static void test_fault_injection_isolation_from_staleness(void)
{
    healink_sensor_state_t state;

    reset_state(&state);
    state.fault_mask = SENSOR_MPU6050;
    state.valid_mask = SENSOR_MPU6050;
    state.mpu6050_ts_ns = 1000000000ULL;

    healink_staleness_update(&state, 5000000000ULL);

    assert((state.fault_mask & SENSOR_MPU6050) != 0U);
    assert((state.stale_mask & SENSOR_MPU6050) == 0U);
    assert((state.valid_mask & SENSOR_MPU6050) == 0U);
}

static void test_stale_fault_recovery_sequence(void)
{
    healink_sensor_state_t state;

    reset_state(&state);
    state.valid_mask = SENSOR_MAX30102;
    state.max30102_ts_ns = 1000000000ULL;

    healink_staleness_update(&state, 1020000000ULL);
    assert((state.stale_mask & SENSOR_MAX30102) != 0U);
    assert((state.valid_mask & SENSOR_MAX30102) == 0U);

    state.max30102_ts_ns = 1030000000ULL;
    state.valid_mask |= SENSOR_MAX30102;
    healink_staleness_update(&state, 1040000000ULL);

    assert((state.stale_mask & SENSOR_MAX30102) == 0U);
    assert((state.valid_mask & SENSOR_MAX30102) != 0U);
}

int main(void)
{
    test_both_sources_agree();
    test_ecg_only();
    test_ppg_only();
    test_temperature_only_is_uncertain();
    test_moderate_disagreement();
    test_strong_disagreement();
    test_stale_source_excluded();
    test_offline_is_not_stale();
    test_stale_clears_valid();
    test_fault_is_not_stale();
    test_no_sources();
    test_quality_aware_agreement();
    test_low_ppg_quality_reduces_confidence();
    test_motion_context_dampens_activity_anomaly();
    test_low_spo2_quality_affects_classifier();
    test_fall_flag_promotes_fall_state();
    test_emergency_escalation_after_confirmed_distress();
    test_fall_causes_immediate_emergency_action();
    test_transient_severe_distress_requires_confirmation();
    test_recovery_clears_latches_only_after_confirmation();
    test_recovery_window_resets_when_alarm_returns();
    test_fall_latch_recovers_after_fall_clears();
    test_fault_injection_isolation_from_staleness();
    test_stale_fault_recovery_sequence();

    puts("HEALINK fusion tests: PASS");

    return 0;
}
