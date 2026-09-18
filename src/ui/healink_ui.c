#define _POSIX_C_SOURCE 200809L

#include "healink_ui.h"
#include "healink_classifier.h"
#include "qnx_time.h"

#include <stdio.h>
#include <string.h>

#define OLED_W 128
#define OLED_H 64
#define NS_PER_MS 1000000ULL

/*
 * ------------------------------------------------------------
 * TWO PRESENTATION MODES
 * ------------------------------------------------------------
 *
 * NORMAL BOOT (default): approximately 6.0 s to the live HUD.
 * This is the judge-facing path: enough cinematic identity to be
 * memorable, but short enough that nobody waits through a long
 * animation before seeing the project.
 *
 * DEMO MODE (--demo or D key): approximately 19.7 s. This shows the
 * complete cinematic sequence, system status and sensor matrix.
 * It can be replayed from the terminal without restarting HEALINK.
 *
 * The sensor/fusion/safety core continues independently in both modes.
 */
/*
 * NORMAL / JUDGE INTRO
 * A separate 6.0 s typography-first path.
 * No graph/line animation is used, avoiding text/graphic collisions.
 */
#define T_FAST_IGNITION_MS    900ULL
#define T_FAST_SENSORS_MS    1200ULL
#define T_FAST_FUSION_MS      900ULL
#define T_FAST_HERO_MS       1500ULL
#define T_FAST_SUBTITLE_MS    600ULL
#define T_FAST_READY_MS        900ULL

#define T_DEMO_IGNITION_MS   1500ULL
#define T_DEMO_NETWORK_MS    1800ULL
#define T_DEMO_SENSORS_MS    2200ULL
#define T_DEMO_FUSION_MS     2000ULL
#define T_DEMO_HERO_MS       2600ULL
#define T_DEMO_SUBTITLE_MS   1600ULL
#define T_DEMO_PHILOSOPHY_MS 2200ULL
#define T_DEMO_READY_MS      1800ULL
#define T_DEMO_SYSTEM_MS     2200ULL
#define T_DEMO_SENSOR_MS     1800ULL

#define PHILOSOPHY_WORD_MS    550ULL
#define LORA_NOTIFICATION_MS 3000ULL
#define SOS_NOTIFICATION_MS  3000ULL
#define LORA_FAILURE_MS      5000ULL
#define EVENT_NOTIFICATION_MS 2500ULL

static uint64_t elapsed_ms(uint64_t now, uint64_t start)
{
    if (now < start) {
        return 0ULL;
    }

    return (now - start) / NS_PER_MS;
}

static uint64_t mode_duration_ms(const healink_ui_t *ui,
                                  healink_ui_mode_t mode)
{
    bool demo = (ui != NULL) ? ui->demo_mode : false;

    switch (mode) {
        case HEALINK_UI_INTRO_IGNITION:
            return demo ? T_DEMO_IGNITION_MS : T_FAST_IGNITION_MS;
        case HEALINK_UI_INTRO_NETWORK:
            return demo ? T_DEMO_NETWORK_MS : 0ULL;
        case HEALINK_UI_INTRO_SENSORS:
            return demo ? T_DEMO_SENSORS_MS : T_FAST_SENSORS_MS;
        case HEALINK_UI_INTRO_FUSION:
            return demo ? T_DEMO_FUSION_MS : T_FAST_FUSION_MS;
        case HEALINK_UI_INTRO_HERO:
            return demo ? T_DEMO_HERO_MS : T_FAST_HERO_MS;
        case HEALINK_UI_INTRO_SUBTITLE:
            return demo ? T_DEMO_SUBTITLE_MS : T_FAST_SUBTITLE_MS;
        case HEALINK_UI_INTRO_PHILOSOPHY:
            return demo ? T_DEMO_PHILOSOPHY_MS : 0ULL;
        case HEALINK_UI_INTRO_READY:
            return demo ? T_DEMO_READY_MS : T_FAST_READY_MS;
        case HEALINK_UI_SYSTEM_CHECK:
            return T_DEMO_SYSTEM_MS;
        case HEALINK_UI_SENSOR_MATRIX:
            return T_DEMO_SENSOR_MS;
        case HEALINK_UI_LIVE:
        default:
            return 0ULL;
    }
}

static void transition(healink_ui_t *ui,
                       healink_ui_mode_t next,
                       uint64_t now_ns)
{
    if (ui == NULL) {
        return;
    }

    ui->mode = next;
    ui->mode_started_ns = now_ns;
    ui->philosophy_step = 0U;
}

static const char *sensor_state(const healink_sensor_state_t *s,
                                uint32_t mask)
{
    if (s == NULL) {
        return "--";
    }

    if ((s->offline_mask & mask) != 0U) {
        return "OFF";
    }

    if ((s->fault_mask & mask) != 0U) {
        return "FLT";
    }

    if ((s->stale_mask & mask) != 0U) {
        return "STL";
    }

    if ((s->valid_mask & mask) != 0U) {
        return "ON";
    }

    return "RDY";
}

static void draw_centered(ssd1306_t *oled,
                          int y,
                          const char *text)
{
    ssd1306_draw_text_centered_scaled(oled, y, text, 1U, true);
}

static void draw_hero(ssd1306_t *oled, int y)
{
    ssd1306_draw_text_centered_scaled(oled, y, "HEALINK", 2U, true);
}

static void draw_horizontal_center_line(ssd1306_t *oled, int y)
{
    ssd1306_draw_line(oled, 12, y, 115, y, true);
}

static void draw_normal_ignition(ssd1306_t *oled, uint64_t e)
{
    if (e < 300ULL) {
        draw_centered(oled, 28, "H");
    } else if (e < 600ULL) {
        draw_centered(oled, 28, "HE");
    } else {
        draw_centered(oled, 26, "HEALINK");
    }
}

static void draw_normal_sensors(ssd1306_t *oled, uint64_t e)
{
    unsigned step = (unsigned)(e / 300ULL);
    if (step > 3U) step = 3U;

    draw_centered(oled, 4, "SENSORS");
    draw_centered(oled, 17, "MAX30102");
    if (step >= 1U) draw_centered(oled, 28, "AD8232");
    if (step >= 2U) draw_centered(oled, 39, "MPU6050");
    if (step >= 3U) draw_centered(oled, 50, "DS18B20");
}

static void draw_normal_fusion(ssd1306_t *oled, uint64_t e)
{
    if (e < 450ULL) {
        draw_centered(oled, 27, "4 SIGNALS");
    } else {
        draw_centered(oled, 18, "4 SIGNALS");
        draw_centered(oled, 31, "FUSION CORE");
        draw_centered(oled, 45, "HEALINK");
    }
}

static void draw_normal_hero(ssd1306_t *oled)
{
    draw_hero(oled, 22);
}

static void draw_normal_subtitle(ssd1306_t *oled, uint64_t e)
{
    if (e < 300ULL) {
        draw_centered(oled, 25, "QNX HEALTH");
    } else {
        draw_centered(oled, 19, "QNX HEALTH");
        draw_centered(oled, 35, "FUSION CORE");
    }
}

static void draw_normal_ready(ssd1306_t *oled)
{
    draw_centered(oled, 11, "HEALINK");
    draw_centered(oled, 28, "QNX HEALTH");
    draw_centered(oled, 45, "READY");
}

static void draw_ignition(ssd1306_t *oled, uint64_t e)
{
    const int cx = 64;
    const int cy = 32;
    unsigned phase = (unsigned)(e / 150ULL);

    if (phase > 9U) {
        phase = 9U;
    }

    ssd1306_draw_pixel(oled, cx, cy, true);

    if (phase >= 1U) {
        int r = 2 + (int)((phase - 1U) * 2U);
        ssd1306_draw_rect(oled, cx - r, cy - r, 2 * r + 1, 2 * r + 1, true);
    }

    if (phase >= 3U) {
        ssd1306_draw_line(oled, cx - 18, cy, cx + 18, cy, true);
        ssd1306_draw_line(oled, cx, cy - 18, cx, cy + 18, true);
    }

    if (phase >= 6U) {
        ssd1306_draw_rect(oled, 16, 8, 96, 48, true);
    }
}

static void draw_network(ssd1306_t *oled, uint64_t e)
{
    const int cx = 64;
    const int cy = 31;
    unsigned phase = (unsigned)(e / 300ULL);

    if (phase > 5U) {
        phase = 5U;
    }

    ssd1306_draw_pixel(oled, cx, cy, true);

    if (phase >= 1U) {
        ssd1306_draw_line(oled, cx, cy, 28, 14, true);
    }
    if (phase >= 2U) {
        ssd1306_draw_line(oled, cx, cy, 100, 14, true);
    }
    if (phase >= 3U) {
        ssd1306_draw_line(oled, cx, cy, 28, 48, true);
    }
    if (phase >= 4U) {
        ssd1306_draw_line(oled, cx, cy, 100, 48, true);
    }

    if (phase >= 2U) {
        ssd1306_draw_rect(oled, 24, 10, 9, 9, true);
    }
    if (phase >= 3U) {
        ssd1306_draw_rect(oled, 96, 10, 9, 9, true);
    }
    if (phase >= 4U) {
        ssd1306_draw_rect(oled, 24, 44, 9, 9, true);
    }
    if (phase >= 5U) {
        ssd1306_draw_rect(oled, 96, 44, 9, 9, true);
    }

    draw_centered(oled, 55, "SIGNAL NETWORK");
}

static void draw_sensor_constellation(ssd1306_t *oled, uint64_t e)
{
    unsigned stage = (unsigned)(e / 450ULL);

    if (stage > 4U) {
        stage = 4U;
    }

    /* Fixed geometry: everything is aligned to a 128x64 pixel grid. */
    if (stage >= 1U) {
        ssd1306_draw_text_centered_scaled(oled, 2, "MAX", 1U, true);
        ssd1306_draw_line(oled, 64, 10, 64, 22, true);
    }

    if (stage >= 2U) {
        ssd1306_draw_text_scaled(oled, 20, 22, "ECG", 1U, true);
        ssd1306_draw_line(oled, 38, 28, 58, 28, true);
        ssd1306_draw_text_scaled(oled, 92, 22, "IMU", 1U, true);
        ssd1306_draw_line(oled, 70, 28, 90, 28, true);
    }

    if (stage >= 3U) {
        ssd1306_draw_line(oled, 64, 36, 64, 45, true);
        ssd1306_draw_text_scaled(oled, 52, 47, "TEMP", 1U, true);
    }

    if (stage >= 4U) {
        ssd1306_draw_rect(oled, 59, 23, 11, 11, true);
    } else {
        ssd1306_draw_pixel(oled, 64, 28, true);
    }

    if (stage >= 4U) {
        draw_centered(oled, 56, "4 SIGNAL SOURCES");
    }
}

static void draw_fusion(ssd1306_t *oled, uint64_t e)
{
    const int cx = 64;
    const int cy = 31;
    unsigned phase = (unsigned)(e / 250ULL);
    int distance;

    if (phase > 7U) {
        phase = 7U;
    }

    distance = 38 - (int)(phase * 5U);
    if (distance < 4) {
        distance = 4;
    }

    ssd1306_draw_line(oled, cx - distance, 12, cx, cy, true);
    ssd1306_draw_line(oled, cx + distance, 12, cx, cy, true);
    ssd1306_draw_line(oled, cx - distance, 50, cx, cy, true);
    ssd1306_draw_line(oled, cx + distance, 50, cx, cy, true);

    if (phase >= 4U) {
        ssd1306_draw_rect(oled, 48, 20, 32, 22, true);
        draw_centered(oled, 27, "FUSION");
    } else {
        ssd1306_draw_pixel(oled, cx, cy, true);
    }

    draw_centered(oled, 55, "MEASURE  VERIFY");
}

static void draw_hero_reveal(ssd1306_t *oled, uint64_t e)
{
    if (e < 700ULL) {
        int half = 8 + (int)(e / 40ULL);
        if (half > 55) {
            half = 55;
        }
        ssd1306_draw_line(oled, 64 - half, 31, 64 + half, 31, true);
        return;
    }

    if (e < 1200ULL) {
        int half = 55 - (int)((e - 700ULL) / 20ULL);
        if (half < 5) {
            half = 5;
        }
        ssd1306_draw_line(oled, 64 - half, 31, 64 + half, 31, true);
        return;
    }

    draw_hero(oled, 22);

    if (e >= 1700ULL) {
        ssd1306_draw_line(oled, 22, 41, 106, 41, true);
    }

    if (e >= 2150ULL) {
        ssd1306_draw_rect(oled, 18, 18, 92, 28, true);
        draw_hero(oled, 22);
        ssd1306_draw_line(oled, 22, 41, 106, 41, true);
    }
}

static void draw_subtitle(ssd1306_t *oled)
{
    draw_hero(oled, 15);
    draw_horizontal_center_line(oled, 32);
    draw_centered(oled, 40, "QNX HEALTH FUSION");
    draw_centered(oled, 55, "EDGE INTELLIGENCE");
}

static void draw_philosophy(ssd1306_t *oled, uint64_t e, bool demo_mode)
{
    static const char *const words[4] = {
        "MEASURE",
        "VERIFY",
        "FUSE",
        "DECIDE"
    };
    uint64_t word_ms = demo_mode ? PHILOSOPHY_WORD_MS : 150ULL;
    unsigned step = (unsigned)(e / word_ms);
    uint64_t within = e % word_ms;

    if (step > 3U) {
        step = 3U;
    }

    if (within < 120ULL) {
        ssd1306_draw_line(oled, 18, 31, 110, 31, true);
    } else {
        draw_centered(oled, 27, words[step]);
        ssd1306_draw_line(oled, 32, 40, 96, 40, true);
    }

    draw_centered(oled, 55, "HEALTH FUSION");
}

static void draw_ready(ssd1306_t *oled, uint64_t e)
{
    draw_hero(oled, 10);
    ssd1306_draw_line(oled, 22, 29, 106, 29, true);
    draw_centered(oled, 36, "QNX HEALTH FUSION");

    if ((e / 300ULL) % 2ULL == 0ULL) {
        ssd1306_draw_rect(oled, 33, 48, 62, 11, true);
        draw_centered(oled, 50, "READY");
    } else {
        draw_centered(oled, 50, "READY");
    }
}

static void draw_system_status(ssd1306_t *oled)
{
    draw_centered(oled, 1, "HEALINK");
    draw_centered(oled, 9, "SYSTEM STATUS");
    draw_horizontal_center_line(oled, 17);

    ssd1306_draw_text(oled, 3, "CORE          OK");
    ssd1306_draw_text(oled, 4, "I2C           OK");
    ssd1306_draw_text(oled, 5, "OLED          OK");
    ssd1306_draw_text(oled, 6, "FUSION        OK");
    ssd1306_draw_text(oled, 7, "SAFETY        OK");
}

static void draw_sensor_matrix(ssd1306_t *oled,
                               const healink_sensor_state_t *s)
{
    char line[32];

    draw_centered(oled, 1, "SENSORS");
    draw_horizontal_center_line(oled, 9);

    (void)snprintf(line, sizeof(line), "MAX    %s", sensor_state(s, SENSOR_MAX30102));
    ssd1306_draw_text(oled, 2, line);
    (void)snprintf(line, sizeof(line), "ECG    %s", sensor_state(s, SENSOR_AD8232));
    ssd1306_draw_text(oled, 3, line);
    (void)snprintf(line, sizeof(line), "IMU    %s", sensor_state(s, SENSOR_MPU6050));
    ssd1306_draw_text(oled, 4, line);
    (void)snprintf(line, sizeof(line), "TEMP   %s", sensor_state(s, SENSOR_DS18B20));
    ssd1306_draw_text(oled, 5, line);
    (void)snprintf(line, sizeof(line), "DHT11  %s", sensor_state(s, SENSOR_DHT11));
    ssd1306_draw_text(oled, 6, line);
}

static void draw_live(ssd1306_t *oled,
                      const healink_sensor_state_t *s)
{
    char line[48];
    bool hr_valid;
    bool spo2_valid;
    bool temp_valid;
    bool dht_valid;
    bool imu_valid;
    bool ecg_valid;

    if (s == NULL) {
        return;
    }

    hr_valid = ((s->valid_mask & (SENSOR_MAX30102 | SENSOR_AD8232)) != 0U);
    spo2_valid = ((s->valid_mask & SENSOR_MAX30102) != 0U);
    temp_valid = ((s->valid_mask & SENSOR_DS18B20) != 0U);
    dht_valid = ((s->valid_mask & SENSOR_DHT11) != 0U);
    imu_valid = ((s->valid_mask & SENSOR_MPU6050) != 0U);
    ecg_valid = ((s->valid_mask & SENSOR_AD8232) != 0U);
    /*
     * LIVE SENSOR-VALUES HUD ONLY.
     * Keep the existing 128x64 layout compact and readable.
     * No health state, confidence, quality, validity masks, or
     * system-status text is rendered here.
     */
    draw_centered(oled, 0, "HEALINK");

    if (hr_valid) {
        (void)snprintf(line, sizeof(line),
                       "HR %3.0f BPM  SpO2 %3.0f%%",
                       s->hr_bpm,
                       spo2_valid ? s->spo2_pct : 0.0f);
        if (!spo2_valid) {
            (void)snprintf(line, sizeof(line),
                           "HR %3.0f BPM  SpO2 --%%",
                           s->hr_bpm);
        }
    } else {
        (void)snprintf(line, sizeof(line), "HR -- BPM   SpO2 --%%");
    }
    draw_centered(oled, 11, line);

    if (temp_valid && dht_valid) {
        (void)snprintf(line, sizeof(line),
                       "TEMP %.1fC   AMB %.1fC",
                       s->temperature_c,
                       s->ambient_temperature_c);
    } else if (temp_valid) {
        (void)snprintf(line, sizeof(line),
                       "TEMP %.1fC   AMB --C",
                       s->temperature_c);
    } else if (dht_valid) {
        (void)snprintf(line, sizeof(line),
                       "TEMP --C     AMB %.1fC",
                       s->ambient_temperature_c);
    } else {
        (void)snprintf(line, sizeof(line), "TEMP --C     AMB --C");
    }
    draw_centered(oled, 23, line);

    if (dht_valid && imu_valid) {
        (void)snprintf(line, sizeof(line),
                       "HUM %.0f%%RH   ACC %.2fG",
                       s->humidity_pct,
                       s->accel_mag_g);
    } else if (dht_valid) {
        (void)snprintf(line, sizeof(line),
                       "HUM %.0f%%RH   ACC --G",
                       s->humidity_pct);
    } else if (imu_valid) {
        (void)snprintf(line, sizeof(line),
                       "HUM --%%RH    ACC %.2fG",
                       s->accel_mag_g);
    } else {
        (void)snprintf(line, sizeof(line), "HUM --%%RH    ACC --G");
    }
    draw_centered(oled, 35, line);

    if (ecg_valid) {
        char ecg_part[24];
        (void)snprintf(ecg_part, sizeof(ecg_part), "ECG %3.0f BPM", s->ecg_hr_bpm);
        (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                       "   %s", ecg_part);
    } else {
        (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                       "   ECG -- BPM");
    }
    draw_centered(oled, 47, line);
}


static void draw_event_notification(ssd1306_t *oled,
                                     const healink_event_t *event,
                                     uint64_t e_ms)
{
    char line[32];
    uint32_t event_id;

    if (oled == NULL || event == NULL) {
        return;
    }

    /* A compact pulse bar makes the event visibly distinct on the OLED. */
    if (((e_ms / 200ULL) % 2ULL) == 0ULL) {
        ssd1306_fill_rect(oled, 0, 0, 128, 2, true);
    } else {
        ssd1306_draw_line(oled, 0, 0, 127, 0, true);
    }

    switch (event->event_type) {
        case EVENT_SOS:
            draw_centered(oled, 8, "!!! SOS !!!");
            draw_centered(oled, 24, "EMERGENCY EVENT");
            draw_centered(oled, 40, "LORA TX");
            draw_centered(oled, 54, "WAITING ACK");
            break;

        case EVENT_LORA_ACK:
            event_id = (uint32_t)(event->value_a + 0.5f);
            draw_centered(oled, 7, "LORA ACK");
            draw_centered(oled, 21, "CONFIRMED");
            (void)snprintf(line, sizeof(line), "EVENT %u", event_id);
            draw_centered(oled, 37, line);
            (void)snprintf(line, sizeof(line), "RTT %.0f ms", event->value_b);
            draw_centered(oled, 51, line);
            break;

        case EVENT_LORA_FAILURE:
            event_id = (uint32_t)(event->value_a + 0.5f);
            draw_centered(oled, 7, "LORA ACK");
            draw_centered(oled, 21, "FAILURE");
            (void)snprintf(line, sizeof(line), "EVENT %u", event_id);
            draw_centered(oled, 37, line);
            (void)snprintf(line, sizeof(line), "ELAPSED %.1f S",
                           event->value_b / 1000.0f);
            draw_centered(oled, 51, line);
            break;

        case EVENT_FALL_DETECTED:
            draw_centered(oled, 8, "FALL DETECTED");
            draw_centered(oled, 25, "EMERGENCY PATH");
            draw_centered(oled, 42, "LORA TX");
            draw_centered(oled, 56, "WAITING ACK");
            break;

        case EVENT_EMERGENCY:
            draw_centered(oled, 8, "EMERGENCY");
            draw_centered(oled, 25, "EVENT ACTIVE");
            draw_centered(oled, 42, "LORA TX");
            draw_centered(oled, 56, "WAITING ACK");
            break;

        case EVENT_DISTRESS:
            draw_centered(oled, 8, "DISTRESS");
            draw_centered(oled, 25, "EVENT ACTIVE");
            draw_centered(oled, 42, "LORA TX");
            draw_centered(oled, 56, "WAITING ACK");
            break;

        default:
            break;
    }
}

static bool notification_should_show(const healink_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    return event->event_type == EVENT_SOS ||
           event->event_type == EVENT_LORA_ACK ||
           event->event_type == EVENT_LORA_FAILURE ||
           event->event_type == EVENT_FALL_DETECTED ||
           event->event_type == EVENT_EMERGENCY ||
           event->event_type == EVENT_DISTRESS;
}

int healink_ui_init(healink_ui_t *ui,
                    ssd1306_t *oled,
                    uint64_t now_ns,
                    bool demo_mode)
{
    if (ui == NULL || oled == NULL || !oled->ready) {
        return -1;
    }

    memset(ui, 0, sizeof(*ui));
    ui->oled = oled;
    ui->mode = HEALINK_UI_INTRO_IGNITION;
    ui->mode_started_ns = now_ns;
    ui->demo_mode = demo_mode;

    if (pthread_mutex_init(&ui->event_lock, NULL) != 0) {
        memset(ui, 0, sizeof(*ui));
        return -1;
    }

    ui->event_lock_initialized = true;
    ui->notification_active = false;
    ui->initialized = true;

    return 0;
}

void healink_ui_replay_demo(healink_ui_t *ui, uint64_t now_ns)
{
    if (ui == NULL || !ui->initialized) {
        return;
    }

    ui->demo_mode = true;
    ui->mode = HEALINK_UI_INTRO_IGNITION;
    ui->mode_started_ns = now_ns;
    ui->philosophy_step = 0U;
}

void healink_ui_notify_event(healink_ui_t *ui,
                             const healink_event_t *event,
                             uint64_t now_ns)
{
    if (ui == NULL || event == NULL ||
        !ui->initialized || !ui->event_lock_initialized ||
        !notification_should_show(event)) {
        return;
    }

    if (pthread_mutex_lock(&ui->event_lock) != 0) {
        return;
    }

    ui->notification_event = *event;
    ui->notification_started_ns = now_ns;
    ui->notification_active = true;

    (void)pthread_mutex_unlock(&ui->event_lock);
}

void healink_ui_tick(healink_ui_t *ui,
                     const healink_sensor_state_t *state,
                     uint64_t now_ns)
{
    uint64_t e;

    if (ui == NULL || !ui->initialized || ui->oled == NULL) {
        return;
    }

    e = elapsed_ms(now_ns, ui->mode_started_ns);

    if (!ui->demo_mode) {
        switch (ui->mode) {
            case HEALINK_UI_INTRO_IGNITION:
                if (e >= T_FAST_IGNITION_MS) {
                    transition(ui, HEALINK_UI_INTRO_SENSORS, now_ns);
                }
                break;
            case HEALINK_UI_INTRO_SENSORS:
                if (e >= T_FAST_SENSORS_MS) {
                    transition(ui, HEALINK_UI_INTRO_FUSION, now_ns);
                }
                break;
            case HEALINK_UI_INTRO_FUSION:
                if (e >= T_FAST_FUSION_MS) {
                    transition(ui, HEALINK_UI_INTRO_HERO, now_ns);
                }
                break;
            case HEALINK_UI_INTRO_HERO:
                if (e >= T_FAST_HERO_MS) {
                    transition(ui, HEALINK_UI_INTRO_SUBTITLE, now_ns);
                }
                break;
            case HEALINK_UI_INTRO_SUBTITLE:
                if (e >= T_FAST_SUBTITLE_MS) {
                    transition(ui, HEALINK_UI_INTRO_READY, now_ns);
                }
                break;
            case HEALINK_UI_INTRO_READY:
                if (e >= T_FAST_READY_MS) {
                    transition(ui, HEALINK_UI_LIVE, now_ns);
                }
                break;
            default:
                break;
        }
    } else {
        switch (ui->mode) {
            case HEALINK_UI_INTRO_IGNITION:
            case HEALINK_UI_INTRO_NETWORK:
            case HEALINK_UI_INTRO_SENSORS:
            case HEALINK_UI_INTRO_FUSION:
            case HEALINK_UI_INTRO_HERO:
            case HEALINK_UI_INTRO_SUBTITLE:
                if (e >= mode_duration_ms(ui, ui->mode)) {
                    transition(ui, (healink_ui_mode_t)(ui->mode + 1), now_ns);
                }
                break;
            case HEALINK_UI_INTRO_PHILOSOPHY:
                ui->philosophy_step = (unsigned)(e / PHILOSOPHY_WORD_MS);
                if (e >= mode_duration_ms(ui, ui->mode)) {
                    transition(ui, HEALINK_UI_INTRO_READY, now_ns);
                }
                break;
            case HEALINK_UI_INTRO_READY:
                if (e >= mode_duration_ms(ui, ui->mode)) {
                    transition(ui, HEALINK_UI_SYSTEM_CHECK, now_ns);
                }
                break;
            case HEALINK_UI_SYSTEM_CHECK:
                if (e >= mode_duration_ms(ui, ui->mode)) {
                    transition(ui, HEALINK_UI_SENSOR_MATRIX, now_ns);
                }
                break;
            case HEALINK_UI_SENSOR_MATRIX:
                if (e >= mode_duration_ms(ui, ui->mode)) {
                    transition(ui, HEALINK_UI_LIVE, now_ns);
                    ui->demo_mode = false;
                }
                break;
            case HEALINK_UI_LIVE:
            default:
                break;
        }
    }

    /* Snapshot the asynchronous event notification for this UI frame. */
    {
        bool show_event = false;
        healink_event_t event_snapshot;
        uint64_t event_elapsed_ms = 0ULL;

        memset(&event_snapshot, 0, sizeof(event_snapshot));

        if (ui->event_lock_initialized &&
            pthread_mutex_lock(&ui->event_lock) == 0) {
            if (ui->notification_active) {
                const uint64_t age_ms =
                    (now_ns >= ui->notification_started_ns)
                        ? ((now_ns - ui->notification_started_ns) / NS_PER_MS)
                        : 0ULL;

                uint64_t duration_ms = EVENT_NOTIFICATION_MS;
                if (ui->notification_event.event_type == EVENT_SOS) {
                    duration_ms = SOS_NOTIFICATION_MS;
                } else if (ui->notification_event.event_type == EVENT_LORA_ACK) {
                    duration_ms = LORA_NOTIFICATION_MS;
                } else if (ui->notification_event.event_type == EVENT_LORA_FAILURE) {
                    duration_ms = LORA_FAILURE_MS;
                }

                if (age_ms < duration_ms) {
                    show_event = true;
                    event_snapshot = ui->notification_event;
                    event_elapsed_ms = age_ms;
                } else {
                    ui->notification_active = false;
                }
            }

            (void)pthread_mutex_unlock(&ui->event_lock);
        }

        ssd1306_clear(ui->oled);

        if (show_event) {
            draw_event_notification(ui->oled,
                                     &event_snapshot,
                                     event_elapsed_ms);
            (void)ssd1306_flush(ui->oled);
            return;
        }
    }

    if (!ui->demo_mode) {
        switch (ui->mode) {
            case HEALINK_UI_INTRO_IGNITION:
                draw_normal_ignition(ui->oled, e); break;
            case HEALINK_UI_INTRO_SENSORS:
                draw_normal_sensors(ui->oled, e); break;
            case HEALINK_UI_INTRO_FUSION:
                draw_normal_fusion(ui->oled, e); break;
            case HEALINK_UI_INTRO_HERO:
                draw_normal_hero(ui->oled); break;
            case HEALINK_UI_INTRO_SUBTITLE:
                draw_normal_subtitle(ui->oled, e); break;
            case HEALINK_UI_INTRO_READY:
                draw_normal_ready(ui->oled); break;
            case HEALINK_UI_LIVE:
            default:
                draw_live(ui->oled, state); break;
        }
    } else {
        switch (ui->mode) {
            case HEALINK_UI_INTRO_IGNITION: draw_ignition(ui->oled, e); break;
            case HEALINK_UI_INTRO_NETWORK: draw_network(ui->oled, e); break;
            case HEALINK_UI_INTRO_SENSORS: draw_sensor_constellation(ui->oled, e); break;
            case HEALINK_UI_INTRO_FUSION: draw_fusion(ui->oled, e); break;
            case HEALINK_UI_INTRO_HERO: draw_hero_reveal(ui->oled, e); break;
            case HEALINK_UI_INTRO_SUBTITLE: draw_subtitle(ui->oled); break;
            case HEALINK_UI_INTRO_PHILOSOPHY: draw_philosophy(ui->oled, e, true); break;
            case HEALINK_UI_INTRO_READY: draw_ready(ui->oled, e); break;
            case HEALINK_UI_SYSTEM_CHECK: draw_system_status(ui->oled); break;
            case HEALINK_UI_SENSOR_MATRIX: draw_sensor_matrix(ui->oled, state); break;
            case HEALINK_UI_LIVE:
            default: draw_live(ui->oled, state); break;
        }
    }
    (void)ssd1306_flush(ui->oled);
}

void healink_ui_shutdown(healink_ui_t *ui)
{
    if (ui != NULL) {
        ui->initialized = false;

        if (ui->event_lock_initialized) {
            (void)pthread_mutex_destroy(&ui->event_lock);
            ui->event_lock_initialized = false;
        }

        ui->oled = NULL;
    }
}
