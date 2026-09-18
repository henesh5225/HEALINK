#ifndef ADS1115_H
#define ADS1115_H

#include <stdint.h>

#include "qnx_i2c.h"

typedef struct {
    healink_i2c_t *i2c_bus;
    uint8_t i2c_address;
} ads1115_t;

int ads1115_init(ads1115_t *adc, healink_i2c_t *i2c_bus, uint8_t i2c_address);
int ads1115_read_single_ain0(ads1115_t *adc, int16_t *raw_sample);
float ads1115_raw_to_volts(int16_t raw_sample);

#endif
