#include "healink_fusion.h"

#include <stddef.h>

#include "healink_config.h"
#include "healink_types.h"

static float clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static bool valid_hr_value(float value)
{
    return (value >= HEALINK_MIN_HR_BPM && value <= HEALINK_MAX_HR_BPM);
}

static bool is_sensor_measurement_valid(const healink_sensor_state_t *state, uint32_t mask)
{
    if ((state->valid_mask & mask) == 0U) return false;
    if ((state->stale_mask & mask) != 0U) return false;
    if ((state->offline_mask & mask) != 0U) return false;
    if ((state->fault_mask & mask) != 0U) return false;
    return true;
}

static float absolute_difference(float a, float b)
{
    float d = a - b;
    return (d < 0.0f) ? -d : d;
}

static float calculate_hr_stability(healink_sensor_state_t *state)
{
    float stability;

    if (state == NULL || state->hr_bpm <= 0.0f) {
        return 0.0f;
    }

    if (!state->hr_history_valid || state->previous_hr_bpm <= 0.0f) {
        stability = 0.60f;
    } else {
        float delta = absolute_difference(state->hr_bpm, state->previous_hr_bpm);
        if (delta <= 2.0f) stability = 1.0f;
        else if (delta <= 5.0f) stability = 0.80f;
        else if (delta <= 10.0f) stability = 0.50f;
        else stability = 0.20f;
    }

    state->previous_hr_bpm = state->hr_bpm;
    state->hr_history_valid = true;
    return stability;
}

static void fuse_heart_rate(healink_sensor_state_t *state)
{
    bool ecg_available = is_sensor_measurement_valid(state, SENSOR_AD8232) && valid_hr_value(state->ecg_hr_bpm);
    bool ppg_available = is_sensor_measurement_valid(state, SENSOR_MAX30102) && valid_hr_value(state->ppg_hr_bpm);
    float source_quality;
    float delta;

    state->hr_bpm = 0.0f;
    state->hr_confidence = 0.0f;
    state->ecg_ppg_delta_bpm = 0.0f;
    state->ecg_ppg_confidence = 0.0f;
    state->hr_stability = 0.0f;
    state->inconsistent_mask &= ~(SENSOR_AD8232 | SENSOR_MAX30102);

    if (!ecg_available && !ppg_available) return;

    if (ecg_available && !ppg_available) {
        state->hr_bpm = state->ecg_hr_bpm;
        source_quality = clamp01(state->ecg_quality);
        state->hr_confidence = 0.65f * source_quality;
    } else if (!ecg_available && ppg_available) {
        state->hr_bpm = state->ppg_hr_bpm;
        source_quality = clamp01(state->ppg_quality);
        state->hr_confidence = 0.65f * source_quality;
    } else {
        delta = absolute_difference(state->ecg_hr_bpm, state->ppg_hr_bpm);
        state->ecg_ppg_delta_bpm = delta;

        if (delta <= 5.0f) {
            state->hr_bpm = (state->ecg_hr_bpm + state->ppg_hr_bpm) * 0.5f;
            state->hr_confidence = 1.00f * ((state->ecg_quality + state->ppg_quality) * 0.5f);
            state->ecg_ppg_confidence = state->hr_confidence;
        } else if (delta <= 10.0f) {
            state->hr_bpm = (state->ecg_hr_bpm + state->ppg_hr_bpm) * 0.5f;
            state->hr_confidence = 0.85f * ((state->ecg_quality + state->ppg_quality) * 0.5f);
            state->ecg_ppg_confidence = state->hr_confidence;
        } else if (delta <= HEALINK_ECG_PPG_MAX_DIFF_BPM) {
            state->hr_bpm = (state->ecg_hr_bpm + state->ppg_hr_bpm) * 0.5f;
            state->hr_confidence = 0.60f * ((state->ecg_quality + state->ppg_quality) * 0.5f);
            state->ecg_ppg_confidence = state->hr_confidence;
        } else {
            state->hr_bpm = state->ecg_hr_bpm;
            state->hr_confidence = 0.20f * clamp01(state->ecg_quality);
            state->ecg_ppg_confidence = 0.0f;
            state->inconsistent_mask |= SENSOR_AD8232 | SENSOR_MAX30102;
        }
    }

    state->hr_confidence = clamp01(state->hr_confidence);
    state->hr_stability = clamp01(calculate_hr_stability(state));
    if (state->hr_stability < 0.50f) {
        state->hr_confidence *= (0.75f + 0.25f * state->hr_stability);
    }
}

static float calculate_spo2_confidence(const healink_sensor_state_t *state)
{
    float quality;
    if (!is_sensor_measurement_valid(state, SENSOR_MAX30102)) return 0.0f;
    if (state->spo2_pct < HEALINK_MIN_SPO2_PCT || state->spo2_pct > HEALINK_MAX_SPO2_PCT) return 0.0f;
    quality = clamp01(state->ppg_quality);
    if (state->spo2_pct < 88.0f) return 0.70f * quality;
    if (state->spo2_pct < 92.0f) return 0.85f * quality;
    return 1.0f * quality;
}

static float calculate_temperature_confidence(const healink_sensor_state_t *state)
{
    if (!is_sensor_measurement_valid(state, SENSOR_DS18B20)) return 0.0f;
    if (state->temperature_c >= -20.0f && state->temperature_c <= 60.0f) return 1.0f;
    return 0.25f;
}

static float calculate_motion_confidence(const healink_sensor_state_t *state)
{
    if (!is_sensor_measurement_valid(state, SENSOR_MPU6050)) return 0.0f;
    if (state->accel_mag_g >= 0.0f && state->accel_mag_g <= 20.0f) return 1.0f;
    return 0.25f;
}

static void calculate_anomaly_score(healink_sensor_state_t *state)
{
    float score = 0.0f;
    float activity_factor = 1.0f;

    if (state->motion_confidence > 0.0f && state->accel_mag_g > 1.25f) activity_factor = 0.65f;

    if (state->hr_confidence > 0.0f && state->hr_bpm > 100.0f) {
        float hr_excess = state->hr_bpm - 100.0f;
        float hr_score = hr_excess / 60.0f;
        if (hr_score > 1.0f) hr_score = 1.0f;
        score += 0.55f * hr_score * activity_factor;
    }

    if (state->spo2_confidence > 0.0f && state->spo2_pct < 92.0f) {
        float deficit = (92.0f - state->spo2_pct) / 12.0f;
        if (deficit > 1.0f) deficit = 1.0f;
        score += 0.65f * deficit;
    }

    if (state->hr_confidence > 0.0f && state->hr_stability < 0.50f) {
        score += 0.15f * (1.0f - state->hr_stability);
    }

    if ((state->inconsistent_mask & (SENSOR_AD8232 | SENSOR_MAX30102)) != 0U) {
        score += 0.25f;
    }

    state->anomaly_score = clamp01(score);
}

static void calculate_overall_confidence(healink_sensor_state_t *state)
{
    float total = 0.0f;
    unsigned count = 0U;
    unsigned physiological_count = 0U;

    if (state->hr_confidence > 0.0f) {
        total += state->hr_confidence;
        ++count;
        ++physiological_count;
    }
    if (state->spo2_confidence > 0.0f) {
        total += state->spo2_confidence;
        ++count;
        ++physiological_count;
    }
    if (state->temperature_confidence > 0.0f) {
        total += state->temperature_confidence;
        ++count;
    }
    if (state->motion_confidence > 0.0f) {
        total += state->motion_confidence;
        ++count;
    }

    state->overall_confidence =
        (count == 0U) ? 0.0f : total / (float)count;

    /* Coverage matters in addition to signal quality. A valid environmental
     * or motion channel is useful evidence, but it cannot by itself support
     * the same confidence as a fused physiological assessment. */
    if (physiological_count == 0U && state->overall_confidence > HEALINK_CONF_MAX_NO_PHYSIOLOGY) {
        state->overall_confidence = HEALINK_CONF_MAX_NO_PHYSIOLOGY;
    } else if (physiological_count == 1U && state->overall_confidence > HEALINK_CONF_MAX_SINGLE_PHYSIOLOGY) {
        state->overall_confidence = HEALINK_CONF_MAX_SINGLE_PHYSIOLOGY;
    }

    if ((state->inconsistent_mask & (SENSOR_AD8232 | SENSOR_MAX30102)) != 0U) {
        state->overall_confidence *= 0.25f;
    }

    state->overall_confidence = clamp01(state->overall_confidence);
}

void healink_fusion_update(healink_sensor_state_t *state)
{
    if (state == NULL) return;
    state->ecg_quality = clamp01(state->ecg_quality);
    state->ppg_quality = clamp01(state->ppg_quality);
    fuse_heart_rate(state);
    state->spo2_confidence = clamp01(calculate_spo2_confidence(state));
    state->temperature_confidence = clamp01(calculate_temperature_confidence(state));
    state->motion_confidence = clamp01(calculate_motion_confidence(state));
    calculate_anomaly_score(state);
    calculate_overall_confidence(state);
}
