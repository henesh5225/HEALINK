#define _POSIX_C_SOURCE 200809L

#include "ads1115.h"

#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "healink_config.h"

#define REG_CONVERSION 0x00u
#define REG_CONFIG     0x01u

#define ADS1115_OS_BIT              0x8000u
#define ADS1115_MUX_AIN0_GND       0x4000u
#define ADS1115_PGA_4096MV          0x0200u
#define ADS1115_MODE_SINGLE         0x0100u
#define ADS1115_COMP_DISABLE        0x0003u
#define ADS1115_CONVERSION_POLL_NS  100000ULL
#define ADS1115_EXTRA_GUARD_NS      1000000ULL

static int rate_to_code(unsigned samples_per_second, uint16_t *code)
{
    if (code == NULL) {
        errno = EINVAL;
        return -1;
    }

    switch (samples_per_second) {
        case 8U:   *code = 0U; break;
        case 16U:  *code = 1U; break;
        case 32U:  *code = 2U; break;
        case 64U:  *code = 3U; break;
        case 128U: *code = 4U; break;
        case 250U: *code = 5U; break;
        case 475U: *code = 6U; break;
        case 860U: *code = 7U; break;
        default:
            errno = EINVAL;
            return -1;
    }

    return 0;
}

static uint64_t conversion_timeout_ns(unsigned samples_per_second)
{
    uint64_t conversion_ns;

    if (samples_per_second == 0U) {
        return 0ULL;
    }

    conversion_ns =
        (1000000000ULL + (uint64_t)samples_per_second - 1ULL) /
        (uint64_t)samples_per_second;

    return conversion_ns + ADS1115_EXTRA_GUARD_NS;
}

static int wait_conversion_ready(ads1115_t *adc, uint64_t timeout_ns)
{
    struct timespec start;
    struct timespec now;
    struct timespec sleep_time;
    uint64_t elapsed_ns;
    uint16_t cfg;

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        return -1;
    }

    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = (long)ADS1115_CONVERSION_POLL_NS;

    for (;;) {
        if (healink_i2c_read_reg16_be(adc->i2c_bus,
                                      adc->i2c_address,
                                      REG_CONFIG,
                                      &cfg) != 0) {
            return -1;
        }

        if ((cfg & ADS1115_OS_BIT) != 0U) {
            return 0;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            return -1;
        }

        elapsed_ns =
            ((uint64_t)(now.tv_sec - start.tv_sec) * 1000000000ULL) +
            (uint64_t)(now.tv_nsec - start.tv_nsec);

        if (elapsed_ns >= timeout_ns) {
            errno = ETIMEDOUT;
            return -1;
        }

        if (nanosleep(&sleep_time, NULL) != 0) {
            return -1;
        }
    }
}

int ads1115_init(ads1115_t *adc,
                 healink_i2c_t *i2c_bus,
                 uint8_t i2c_address)
{
    uint16_t cfg;

    if (adc == NULL || i2c_bus == NULL || i2c_bus->file_descriptor < 0) {
        errno = EINVAL;
        return -1;
    }

    adc->i2c_bus = i2c_bus;
    adc->i2c_address = i2c_address;

    if (healink_i2c_read_reg16_be(i2c_bus,
                                  i2c_address,
                                  REG_CONFIG,
                                  &cfg) != 0) {
        return -1;
    }

    (void)cfg;
    return 0;
}

int ads1115_read_single_ain0(ads1115_t *adc, int16_t *raw_sample)
{
    uint16_t rate_code;
    uint16_t cfg;
    uint16_t value;
    uint64_t timeout_ns;

    if (adc == NULL ||
        adc->i2c_bus == NULL ||
        adc->i2c_bus->file_descriptor < 0 ||
        raw_sample == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (rate_to_code(
            HEALINK_ADS1115_DATA_RATE_SPS,
            &rate_code) != 0) {
        return -1;
    }

    /*
     * ADS1115 config:
     *   OS   = 1          start single conversion
     *   MUX  = 100        AIN0 - GND
     *   PGA  = 001        +/-4.096 V
     *   MODE = 1          single-shot
     *   DR   = rate_code  configured ADC rate
     *   COMP = disabled
     *
     * 0x0200 explicitly selects PGA=001.  The previous implementation
     * used PGA=000 while converting results with the +/-4.096 V scale.
     */
    cfg = (uint16_t)(ADS1115_OS_BIT |
                     ADS1115_MUX_AIN0_GND |
                     ADS1115_PGA_4096MV |
                     ADS1115_MODE_SINGLE |
                     (uint16_t)(rate_code << 5U) |
                     ADS1115_COMP_DISABLE);

    if (healink_i2c_write_reg16_be(
            adc->i2c_bus,
            adc->i2c_address,
            REG_CONFIG,
            cfg) != 0) {
        return -1;
    }

    timeout_ns = conversion_timeout_ns(
        HEALINK_ADS1115_DATA_RATE_SPS);

    if (wait_conversion_ready(adc, timeout_ns) != 0) {
        return -1;
    }

    if (healink_i2c_read_reg16_be(
            adc->i2c_bus,
            adc->i2c_address,
            REG_CONVERSION,
            &value) != 0) {
        return -1;
    }

    *raw_sample = (int16_t)value;

    return 0;
}

float ads1115_raw_to_volts(int16_t raw_sample)
{
    return ((float)raw_sample * 4.096f) / 32768.0f;
}
