#define _POSIX_C_SOURCE 200809L
#include "ad8232.h"

#include <stddef.h>
#include <errno.h>
#include <math.h>

#include "healink_config.h"
#include "rpi_gpio.h"

int ad8232_init(ad8232_t *ecg_sensor, ads1115_t *adc)
{
    if (ecg_sensor == NULL || adc == NULL) {
        errno = EINVAL;
        return -1;
    }
    ecg_sensor->adc = adc;
    ecg_sensor->bpm = 0.0f;
    ecg_sensor->baseline = 0.0f;
    ecg_sensor->prev_value = 0.0f;
    ecg_sensor->threshold = 0.20f;
    ecg_sensor->last_peak_ns = 0ULL;
    ecg_sensor->last_ts_ns = 0ULL;
    ecg_sensor->valid = false;
    ecg_sensor->lead_status_available = false;
    ecg_sensor->leads_on = true;
    ecg_sensor->signal_quality = 0.0f;

#if HEALINK_GPIO_AD8232_LOD_MINUS >= 0 || HEALINK_GPIO_AD8232_LOD_PLUS >= 0
    {
        bool configured = true;

#if HEALINK_GPIO_AD8232_LOD_MINUS >= 0
        if (rpi_gpio_set_input(HEALINK_GPIO_AD8232_LOD_MINUS) != 0) {
            configured = false;
        }
#endif

#if HEALINK_GPIO_AD8232_LOD_PLUS >= 0
        if (rpi_gpio_set_input(HEALINK_GPIO_AD8232_LOD_PLUS) != 0) {
            configured = false;
        }
#endif

        ecg_sensor->lead_status_available = configured;
    }
#endif

    return 0;
}

int ad8232_sample(ad8232_t *ecg_sensor, uint64_t sample_time_ns)
{
    int16_t raw_adc_value;
    float volts;
    float hp;
    float delta;
    bool lead_status_ok = true;

    if (ecg_sensor == NULL || ecg_sensor->adc == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (ads1115_read_single_ain0(ecg_sensor->adc, &raw_adc_value) != 0) {
        ecg_sensor->valid = false;
        return -1;
    }

#if HEALINK_GPIO_AD8232_LOD_MINUS >= 0 || HEALINK_GPIO_AD8232_LOD_PLUS >= 0
    if (ecg_sensor->lead_status_available) {
        int level_minus = 0;
        int level_plus = 0;

#if HEALINK_GPIO_AD8232_LOD_MINUS >= 0
        if (rpi_gpio_read_level(HEALINK_GPIO_AD8232_LOD_MINUS, &level_minus) != 0) {
            lead_status_ok = false;
        }
#endif

#if HEALINK_GPIO_AD8232_LOD_PLUS >= 0
        if (rpi_gpio_read_level(HEALINK_GPIO_AD8232_LOD_PLUS, &level_plus) != 0) {
            lead_status_ok = false;
        }
#endif

        /*
         * AD8232 LOD outputs are active high for a lead-off condition.
         * In AC lead-off mode LOD+ reports either disconnected input; in
         * DC mode LOD+ / LOD- identify the corresponding disconnected input.
         */
        ecg_sensor->leads_on = lead_status_ok &&
#if HEALINK_GPIO_AD8232_LOD_MINUS >= 0
                        (level_minus == 0) &&
#endif
                        (level_plus == 0);

        if (!lead_status_ok) {
            ecg_sensor->valid = false;
        }
    }
#endif

    volts = ads1115_raw_to_volts(raw_adc_value);
    if (ecg_sensor->baseline == 0.0f) ecg_sensor->baseline = volts;
    ecg_sensor->baseline += 0.002f * (volts - ecg_sensor->baseline);
    hp = volts - ecg_sensor->baseline;
    delta = hp - ecg_sensor->prev_value;

    /* Simple QRS peak detector. This is intentionally conservative; final tuning happens on real ECG data. */
    if (hp > ecg_sensor->threshold && delta > 0.0f &&
        (ecg_sensor->last_peak_ns == 0ULL || sample_time_ns - ecg_sensor->last_peak_ns > 300000000ULL)) {
        if (ecg_sensor->last_peak_ns != 0ULL) {
            float sec = (float)(sample_time_ns - ecg_sensor->last_peak_ns) / 1.0e9f;
            if (sec > 0.30f && sec < 2.0f) {
                ecg_sensor->bpm = 60.0f / sec;
            }
        }
        ecg_sensor->last_peak_ns = sample_time_ns;
    }

    /* Slowly adapt the threshold to avoid depending on a fixed electrode amplitude. */
    ecg_sensor->threshold = 0.99f * ecg_sensor->threshold + 0.01f * fmaxf(0.10f, fabsf(hp) * 1.8f);

    {
        float amplitude = fabsf(hp);
        if (amplitude < 0.02f) {
            ecg_sensor->signal_quality = 0.0f;
        } else if (amplitude < 0.10f) {
            ecg_sensor->signal_quality = amplitude / 0.10f;
        } else if (amplitude <= 1.50f) {
            ecg_sensor->signal_quality = 1.0f;
        } else {
            ecg_sensor->signal_quality = 0.0f;
        }
    }

    ecg_sensor->prev_value = hp;
    ecg_sensor->last_ts_ns = sample_time_ns;

    /*
     * A valid ECG rate is publishable only when any configured lead-off
     * status is successfully readable and reports both inputs connected.
     * When lead-off GPIOs are not assigned, the status is explicitly
     * unavailable and does not invalidate an otherwise usable ECG rate.
     */
    ecg_sensor->valid =
        (ecg_sensor->signal_quality > 0.0f &&
         ecg_sensor->bpm >= 30.0f && ecg_sensor->bpm <= 240.0f) &&
        (!ecg_sensor->lead_status_available ||
         (lead_status_ok && ecg_sensor->leads_on));

    return 0;
}

void ad8232_get(
    ad8232_t *ecg_sensor,
    float *heart_rate_bpm,
    float *signal_quality,
    bool *valid,
    bool *lead_status_available,
    bool *leads_on,
    uint64_t *ts_ns)
{
    if (ecg_sensor == NULL) {
        return;
    }

    if (heart_rate_bpm != NULL) {
        *heart_rate_bpm = ecg_sensor->bpm;
    }

    if (signal_quality != NULL) {
        *signal_quality = ecg_sensor->signal_quality;
    }

    if (valid != NULL) {
        *valid = ecg_sensor->valid;
    }

    if (lead_status_available != NULL) {
        *lead_status_available = ecg_sensor->lead_status_available;
    }

    if (leads_on != NULL) {
        *leads_on = ecg_sensor->leads_on;
    }

    if (ts_ns != NULL) {
        *ts_ns = ecg_sensor->last_ts_ns;
    }
}
