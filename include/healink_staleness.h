#ifndef HEALINK_STALENESS_H
#define HEALINK_STALENESS_H

#include <stdint.h>
#include "healink_types.h"

void healink_staleness_update(healink_sensor_state_t *sensor_state, uint64_t current_time_ns);

#endif
