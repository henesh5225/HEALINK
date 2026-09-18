#ifndef HEALINK_UI_H
#define HEALINK_UI_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "healink_types.h"
#include "ssd1306.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HEALINK_UI_INTRO_IGNITION = 0,
    HEALINK_UI_INTRO_NETWORK,
    HEALINK_UI_INTRO_SENSORS,
    HEALINK_UI_INTRO_FUSION,
    HEALINK_UI_INTRO_HERO,
    HEALINK_UI_INTRO_SUBTITLE,
    HEALINK_UI_INTRO_PHILOSOPHY,
    HEALINK_UI_INTRO_READY,
    HEALINK_UI_SYSTEM_CHECK,
    HEALINK_UI_SENSOR_MATRIX,
    HEALINK_UI_LIVE
} healink_ui_mode_t;

typedef struct {
    ssd1306_t *oled;
    healink_ui_mode_t mode;
    uint64_t mode_started_ns;
    unsigned philosophy_step;
    bool demo_mode;
    bool initialized;
    bool last_frame_valid;

    /* Thread-safe event notification shared with the emergency worker. */
    pthread_mutex_t event_lock;
    bool event_lock_initialized;
    bool notification_active;
    healink_event_t notification_event;
    uint64_t notification_started_ns;
} healink_ui_t;

int healink_ui_init(healink_ui_t *ui,
                    ssd1306_t *oled,
                    uint64_t now_ns,
                    bool demo_mode);
void healink_ui_replay_demo(healink_ui_t *ui, uint64_t now_ns);
void healink_ui_notify_event(healink_ui_t *ui,
                             const healink_event_t *event,
                             uint64_t now_ns);
void healink_ui_tick(healink_ui_t *ui,
                     const healink_sensor_state_t *state,
                     uint64_t now_ns);
void healink_ui_shutdown(healink_ui_t *ui);

#ifdef __cplusplus
}
#endif

#endif /* HEALINK_UI_H */
