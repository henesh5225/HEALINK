#include "healink_safety.h"

#include <stddef.h>

#include "healink_config.h"

static float clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static bool severe_distress(const healink_sensor_state_t *state)
{
    bool has_low_spo2 =
        state->spo2_confidence > 0.0f &&
        state->spo2_pct > 0.0f &&
        state->spo2_pct < 88.0f;

    bool has_high_anomaly =
        state->anomaly_score >= 0.75f &&
        state->overall_confidence >= 0.35f;

    return has_low_spo2 || has_high_anomaly;
}

uint32_t healink_safety_update(healink_sensor_state_t *state)
{
    uint32_t safety_actions = HEALINK_SAFETY_ACTION_NONE;
    bool is_distressed;
    bool is_severe;

    if (state == NULL) {
        return HEALINK_SAFETY_ACTION_NONE;
    }

    state->distress_confirm_count =
        (uint8_t)(state->distress_confirm_count);
    state->recovery_confirm_count =
        (uint8_t)(state->recovery_confirm_count);

    /* A fall is an immediate safety escalation. */
    if (state->fall_event ||
        state->health_state == HEALTH_FALL) {

        if (!state->emergency_latched) {
            state->emergency_latched = true;
            safety_actions |= HEALINK_SAFETY_ACTION_EMERGENCY;
        }

        state->alert_latched = true;
        state->distress_confirm_count = 0U;
        state->recovery_confirm_count = 0U;
        state->fall_confidence = clamp01(state->fall_confidence);
        return safety_actions;
    }

    is_distressed = (state->health_state == HEALTH_DISTRESS);
    is_severe = is_distressed && severe_distress(state);

    if (is_severe) {
        if (state->distress_confirm_count < 255U) {
            ++state->distress_confirm_count;
        }
        state->recovery_confirm_count = 0U;

        if (state->distress_confirm_count == 1U) {
            safety_actions |= HEALINK_SAFETY_ACTION_DISTRESS;
        }

        if (state->distress_confirm_count >=
            HEALINK_DISTRESS_CONFIRM_CYCLES &&
            !state->emergency_latched) {

            state->emergency_latched = true;
            state->alert_latched = true;
            safety_actions |= HEALINK_SAFETY_ACTION_EMERGENCY;
        }

        return safety_actions;
    }

    if (is_distressed) {
        state->recovery_confirm_count = 0U;
        state->distress_confirm_count = 0U;

        if (!state->alert_latched) {
            state->alert_latched = true;
            safety_actions |= HEALINK_SAFETY_ACTION_DISTRESS;
        }

        return safety_actions;
    }

    state->distress_confirm_count = 0U;

    if (state->health_state == HEALTH_STRESSED) {
        state->recovery_confirm_count = 0U;

        if (!state->alert_latched) {
            state->alert_latched = true;
            safety_actions |= HEALINK_SAFETY_ACTION_ALERT;
        }

        return safety_actions;
    }

    /* Stable non-alarming states clear the latches after a short confirmation window. */
    if (state->health_state == HEALTH_NORMAL ||
        state->health_state == HEALTH_RESTING ||
        state->health_state == HEALTH_ACTIVE) {

        if (state->recovery_confirm_count < 255U) {
            ++state->recovery_confirm_count;
        }

        if (state->recovery_confirm_count >=
            HEALINK_RECOVERY_CONFIRM_CYCLES) {

            if (state->emergency_latched ||
                state->alert_latched) {
                safety_actions |= HEALINK_SAFETY_ACTION_RECOVERY;
            }

            state->emergency_latched = false;
            state->alert_latched = false;
            state->recovery_confirm_count = 0U;
        }
    } else {
        state->recovery_confirm_count = 0U;
    }

    return safety_actions;
}
