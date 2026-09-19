#define _POSIX_C_SOURCE 200809L

#include "mq135.h"
#include "rpi_gpio.h"

#include <errno.h>
#include <string.h>

#define MQ135_STABILITY_SAMPLES 3U

int mq135_init(mq135_t *sensor, int gpio_pin, int active_level)
{
    if (sensor == NULL || gpio_pin < 0 ||
        (active_level != 0 && active_level != 1)) {
        errno = EINVAL;
        return MQ135_ERR;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->gpio_pin = gpio_pin;
    sensor->active_level = active_level;
    sensor->raw_level = -1;
    sensor->candidate_level = -1;

    if (rpi_gpio_set_input(gpio_pin) != 0) {
        return MQ135_ERR;
    }

    return MQ135_OK;
}

int mq135_read(mq135_t *sensor, uint64_t sample_time_ns)
{
    int level = -1;

    if (sensor == NULL) {
        errno = EINVAL;
        return MQ135_ERR;
    }

    if (rpi_gpio_read_level(sensor->gpio_pin, &level) != 0) {
        sensor->valid = false;
        return MQ135_ERR;
    }

    if (level != 0 && level != 1) {
        errno = EPROTO;
        sensor->valid = false;
        return MQ135_ERR;
    }

    ++sensor->sample_count;
    sensor->valid = true;
    sensor->raw_level = level;

    if (sensor->candidate_level != level) {
        sensor->candidate_level = level;
        sensor->consecutive_same = 1U;
    } else if (sensor->consecutive_same < MQ135_STABILITY_SAMPLES) {
        ++sensor->consecutive_same;
    }

    if (sensor->consecutive_same >= MQ135_STABILITY_SAMPLES) {
        bool new_asserted = (sensor->candidate_level == sensor->active_level);

        if (!sensor->stable_valid) {
            sensor->asserted = new_asserted;
            sensor->stable_valid = true;
            sensor->last_change_ts_ns = sample_time_ns;
        } else if (new_asserted != sensor->asserted) {
            sensor->asserted = new_asserted;
            ++sensor->transition_count;
            sensor->last_change_ts_ns = sample_time_ns;
        }
    }

    return MQ135_OK;
}

void mq135_get(const mq135_t *sensor,
               int *raw_level,
               bool *asserted,
               bool *valid,
               uint64_t *sample_count,
               uint64_t *transition_count,
               uint64_t *last_change_ts_ns)
{
    if (sensor == NULL) {
        return;
    }
    if (raw_level != NULL) *raw_level = sensor->raw_level;
    if (asserted != NULL) *asserted = sensor->asserted;
    if (valid != NULL) *valid = sensor->valid && sensor->stable_valid;
    if (sample_count != NULL) *sample_count = sensor->sample_count;
    if (transition_count != NULL) *transition_count = sensor->transition_count;
    if (last_change_ts_ns != NULL) *last_change_ts_ns = sensor->last_change_ts_ns;
}

void mq135_close(mq135_t *sensor)
{
    if (sensor != NULL) sensor->valid = false;
}
