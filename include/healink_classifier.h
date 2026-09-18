#ifndef HEALINK_CLASSIFIER_H
#define HEALINK_CLASSIFIER_H

#include "healink_types.h"

void healink_classifier_update(healink_sensor_state_t *sensor_state);
const char *healink_health_state_str(healink_health_state_t sensor_state);
const char *healink_activity_str(healink_activity_t activity);

#endif
