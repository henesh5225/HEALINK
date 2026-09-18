#ifndef AD8232_H
#define AD8232_H

#include <stdbool.h>
#include <stdint.h>
#include "ads1115.h"

typedef struct {
    ads1115_t *adc;
    float bpm;
    float baseline;
    float prev_value;
    float threshold;
    uint64_t last_peak_ns;
    uint64_t last_ts_ns;
    bool valid;
    bool lead_status_available;
    bool leads_on;
    float signal_quality;
} ad8232_t;

int ad8232_init(ad8232_t *ecg_sensor, ads1115_t *adc);
int ad8232_sample(ad8232_t *ecg_sensor, uint64_t sample_time_ns);
void ad8232_get(ad8232_t *ecg_sensor, float *bpm, float *signal_quality, bool *valid, bool *lead_status_available, bool *leads_on, uint64_t *sample_timestamp_ns);

#endif
