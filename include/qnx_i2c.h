#ifndef QNX_I2C_H
#define QNX_I2C_H

#include <stddef.h>
#include <stdint.h>


typedef struct {
    int file_descriptor;
    uint32_t speed_hz;
} healink_i2c_t;


int healink_i2c_open(healink_i2c_t *i2c_bus,
                     const char *device_path,
                     uint32_t speed_hz);

void healink_i2c_close(healink_i2c_t *i2c_bus);


int healink_i2c_write(healink_i2c_t *i2c_bus,
                      uint8_t device_address,
                      const uint8_t *buffer,
                      size_t length);

int healink_i2c_read(healink_i2c_t *i2c_bus,
                     uint8_t device_address,
                     uint8_t *buffer,
                     size_t length);

int healink_i2c_write_read(healink_i2c_t *i2c_bus,
                           uint8_t device_address,
                           const uint8_t *write_data,
                           size_t write_len,
                           uint8_t *read_data,
                           size_t read_len);


int healink_i2c_write_reg8(healink_i2c_t *i2c_bus,
                           uint8_t device_address,
                           uint8_t register_address,
                           uint8_t value);

int healink_i2c_read_reg8(healink_i2c_t *i2c_bus,
                          uint8_t device_address,
                          uint8_t register_address,
                          uint8_t *value);

int healink_i2c_read_regs(healink_i2c_t *i2c_bus,
                          uint8_t device_address,
                          uint8_t register_address,
                          uint8_t *buffer,
                          size_t length);

int healink_i2c_write_reg16_be(healink_i2c_t *i2c_bus,
                               uint8_t device_address,
                               uint8_t register_address,
                               uint16_t value);

int healink_i2c_read_reg16_be(healink_i2c_t *i2c_bus,
                              uint8_t device_address,
                              uint8_t register_address,
                              uint16_t *value);

#endif
