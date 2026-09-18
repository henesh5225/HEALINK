#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>
#include <stdint.h>

#include "qnx_i2c.h"

typedef struct {
    healink_i2c_t *i2c_bus;
    uint8_t i2c_address;
    uint8_t who_am_i;
    float ax_g;
    float ay_g;
    float az_g;
    float mag_g;
    bool valid;
    bool fall_event;
    unsigned impact_count;
    unsigned recovery_count;
    uint64_t impact_start_ns;
    uint64_t last_fall_detect_ns;
    uint64_t last_ts_ns;
} mpu6050_t;

int mpu6050_init(mpu6050_t *sensor,
                 healink_i2c_t *i2c_bus,
                 uint8_t i2c_address);
int mpu6050_read(mpu6050_t *sensor, uint64_t sample_time_ns);

#endif
