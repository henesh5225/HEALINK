#ifndef MAX30102_H
#define MAX30102_H

#include <stdbool.h>
#include <stdint.h>

#include "healink_config.h"
#include "healink_types.h"
#include "qnx_i2c.h"

typedef struct {
    healink_i2c_t *i2c_bus;
    uint8_t i2c_address;
    float hr_bpm;
    float spo2_pct;
    float ir_dc;
    float red_dc;
    float ir_ac;
    float red_ac;
    float signal_quality;
    float ir_samples[HEALINK_MAX30102_WINDOW];
    float red_samples[HEALINK_MAX30102_WINDOW];
    unsigned sample_count;
    uint64_t last_ts_ns;
    bool valid;
} max30102_t;

int max30102_init(max30102_t *sensor,
                  healink_i2c_t *i2c_bus,
                  uint8_t i2c_address);
int max30102_read_sample(max30102_t *sensor, uint64_t sample_time_ns);
void max30102_get(max30102_t *sensor,
                  float *hr_bpm,
                  float *spo2_pct,
                  float *signal_quality,
                  bool *valid,
                  uint64_t *ts_ns);

#endif
