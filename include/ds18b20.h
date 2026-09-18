#ifndef DS18B20_H
#define DS18B20_H

#include <stdbool.h>
#include <stdint.h>

/*
 * DS18B20 on Raspberry Pi 4 BCM2711.
 *
 * Hardware:
 *   DATA -> HEALINK_GPIO_DS18B20 (GPIO4 by default)
 *   DATA -> 3.3 V through an external 4.7 kOhm pull-up
 *   VDD  -> 3.3 V
 *   GND  -> GND
 */

typedef enum {
    DS18B20_OK = 0,
    DS18B20_ERROR = -1,
    DS18B20_NO_SENSOR = -2,
    DS18B20_CRC_ERROR = -3,
    DS18B20_INVALID = -4
} ds18b20_result_t;

typedef struct {
    int gpio_pin;
    float temperature_c;
    bool valid;
    uint64_t last_ts_ns;
} ds18b20_t;

int ds18b20_init(ds18b20_t *sensor, int gpio_pin);
int ds18b20_read(ds18b20_t *sensor, uint64_t sample_time_ns);

#endif /* DS18B20_H */
