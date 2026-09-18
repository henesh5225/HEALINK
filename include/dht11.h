#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>
#include <stdbool.h>

#include "healink_config.h"

typedef enum {
    DHT11_OK = 0,
    DHT11_ERROR = -1,
    DHT11_TIMEOUT = -2,
    DHT11_CHECKSUM = -3
} dht11_result_t;

typedef struct {
    int temperature_c;
    int humidity_pct;
    uint64_t last_ts_ns;
    bool valid;
} dht11_t;

int dht11_init(dht11_t *sensor, int gpio_pin);
int dht11_read(dht11_t *sensor, uint64_t sample_time_ns);
void dht11_close(dht11_t *sensor);

#endif /* DHT11_H */
