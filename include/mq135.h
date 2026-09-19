#ifndef MQ135_H
#define MQ135_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MQ135_OK = 0,
    MQ135_ERR = -1
} mq135_status_t;

typedef struct {
    int gpio_pin;
    int active_level;
    int raw_level;
    int candidate_level;
    unsigned consecutive_same;
    bool asserted;
    bool stable_valid;
    bool valid;
    uint64_t sample_count;
    uint64_t transition_count;
    uint64_t last_change_ts_ns;
} mq135_t;

int mq135_init(mq135_t *sensor, int gpio_pin, int active_level);
int mq135_read(mq135_t *sensor, uint64_t sample_time_ns);
void mq135_get(const mq135_t *sensor,
               int *raw_level,
               bool *asserted,
               bool *valid,
               uint64_t *sample_count,
               uint64_t *transition_count,
               uint64_t *last_change_ts_ns);
void mq135_close(mq135_t *sensor);

#endif /* MQ135_H */
