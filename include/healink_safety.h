#ifndef HEALINK_SAFETY_H
#define HEALINK_SAFETY_H

#include <stdint.h>

#include "healink_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HEALINK_SAFETY_ACTION_NONE      0u
#define HEALINK_SAFETY_ACTION_ALERT     (1u << 0)
#define HEALINK_SAFETY_ACTION_DISTRESS  (1u << 1)
#define HEALINK_SAFETY_ACTION_EMERGENCY (1u << 2)
#define HEALINK_SAFETY_ACTION_RECOVERY  (1u << 3)

/*
 * Deterministic safety policy. The function updates safety latches
 * in the supplied sensor_state and returns a bitmask of one-shot actions
 * that should be published to the event queue.
 */
uint32_t healink_safety_update(healink_sensor_state_t *sensor_state);

#ifdef __cplusplus
}
#endif

#endif /* HEALINK_SAFETY_H */
