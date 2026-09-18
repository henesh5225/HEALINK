#include "healink_classifier.h"

#include <stddef.h>
#include "healink_config.h"
#include "healink_types.h"

const char *healink_health_state_str(healink_health_state_t state)
{
    switch (state) {
        case HEALTH_NORMAL: return "NORMAL";
        case HEALTH_RESTING: return "RESTING";
        case HEALTH_ACTIVE: return "ACTIVE";
        case HEALTH_STRESSED: return "STRESSED";
        case HEALTH_DISTRESS: return "DISTRESS";
        case HEALTH_FALL: return "FALL";
        case HEALTH_SENSOR_UNCERTAIN: return "SENSOR_UNCERTAIN";
        case HEALTH_UNKNOWN: default: return "UNKNOWN";
    }
}

const char *healink_activity_str(healink_activity_t activity)
{
    switch (activity) {
        case ACTIVITY_RESTING: return "RESTING";
        case ACTIVITY_ACTIVE: return "ACTIVE";
        case ACTIVITY_FALL: return "FALL";
        case ACTIVITY_UNKNOWN: default: return "UNKNOWN";
    }
}

void healink_classifier_update(healink_sensor_state_t *sensor_state)
{
    bool active = (sensor_state != NULL && sensor_state->motion_confidence > 0.0f && sensor_state->accel_mag_g > 1.25f);
    bool physiological_observed;
    bool physiological_uncertain;

    if (sensor_state == NULL) return;

    sensor_state->fall_confidence = sensor_state->fall_event ? 1.0f : 0.0f;

    if (sensor_state->fall_event) {
        sensor_state->health_state = HEALTH_FALL;
        sensor_state->activity = ACTIVITY_FALL;
        return;
    }

    physiological_observed =
        (sensor_state->hr_confidence > 0.0f) ||
        (sensor_state->spo2_confidence > 0.0f);

    physiological_uncertain =
        !physiological_observed ||
        ((sensor_state->inconsistent_mask & (SENSOR_AD8232 | SENSOR_MAX30102)) != 0U) ||
        (sensor_state->hr_confidence > 0.0f &&
         sensor_state->hr_confidence < HEALINK_CONF_PHYSIOLOGY_THRESHOLD &&
         sensor_state->hr_bpm > 0.0f) ||
        (sensor_state->overall_confidence < HEALINK_CONF_PHYSIOLOGY_THRESHOLD);

    if (physiological_uncertain) {
        sensor_state->health_state = HEALTH_SENSOR_UNCERTAIN;
        sensor_state->activity = active ? ACTIVITY_ACTIVE : ACTIVITY_UNKNOWN;
        return;
    }

    if (sensor_state->overall_confidence <= 0.0f) {
        sensor_state->health_state = HEALTH_SENSOR_UNCERTAIN;
        sensor_state->activity = ACTIVITY_UNKNOWN;
        return;
    }

    if (sensor_state->spo2_confidence > 0.0f && sensor_state->spo2_pct < 88.0f) {
        sensor_state->health_state = HEALTH_DISTRESS;
    } else if (sensor_state->anomaly_score >= 0.75f && !active) {
        sensor_state->health_state = HEALTH_DISTRESS;
    } else if (sensor_state->anomaly_score >= 0.40f) {
        sensor_state->health_state = HEALTH_STRESSED;
    } else if (active) {
        sensor_state->health_state = HEALTH_ACTIVE;
    } else {
        sensor_state->health_state = HEALTH_RESTING;
    }

    sensor_state->activity = active ? ACTIVITY_ACTIVE : ACTIVITY_RESTING;
}
