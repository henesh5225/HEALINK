#include "dht11.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>

#define BCM2711_GPIO_BASE  0xFE200000ULL
#define GPIO_MAP_SIZE      0x1000U
#define GPFSEL0            0U
#define GPSET0             7U
#define GPSET1             8U
#define GPCLR0             10U
#define GPCLR1             11U
#define GPLEV0             13U
#define GPLEV1             14U

static volatile uint32_t *g_gpio_base = NULL;
static uint64_t g_cycles_per_second = 0ULL;
static int g_gpio = HEALINK_GPIO_DHT11;

static void delay_us(unsigned int us)
{
    nanospin_ns((uint64_t)us * 1000ULL);
}

static uint64_t get_cycles(void)
{
    return ClockCycles();
}

static uint64_t elapsed_us(uint64_t start_time, uint64_t end_time)
{
    uint64_t cycles = end_time - start_time;

    if (g_cycles_per_second == 0ULL) {
        return 0ULL;
    }

    return (cycles * 1000000ULL) / g_cycles_per_second;
}

static void gpio_set_input(int gpio_pin)
{
    unsigned int reg = (unsigned int)(gpio_pin / 10);
    unsigned int shift = (unsigned int)((gpio_pin % 10) * 3);
    uint32_t value = g_gpio_base[reg];

    value &= ~(7U << shift);
    g_gpio_base[reg] = value;
}

static void gpio_set_output(int gpio_pin)
{
    unsigned int reg = (unsigned int)(gpio_pin / 10);
    unsigned int shift = (unsigned int)((gpio_pin % 10) * 3);
    uint32_t value = g_gpio_base[reg];

    value &= ~(7U << shift);
    value |= (1U << shift);
    g_gpio_base[reg] = value;
}

static void gpio_set_high(int gpio_pin)
{
    if (gpio_pin < 32) {
        g_gpio_base[GPSET0] = (uint32_t)(1U << gpio_pin);
    } else {
        g_gpio_base[GPSET1] = (uint32_t)(1U << (gpio_pin - 32));
    }
}

static void gpio_set_low(int gpio_pin)
{
    if (gpio_pin < 32) {
        g_gpio_base[GPCLR0] = (uint32_t)(1U << gpio_pin);
    } else {
        g_gpio_base[GPCLR1] = (uint32_t)(1U << (gpio_pin - 32));
    }
}

static int gpio_read(int gpio_pin)
{
    if (gpio_pin < 32) {
        return (int)((g_gpio_base[GPLEV0] >> gpio_pin) & 1U);
    }

    return (int)((g_gpio_base[GPLEV1] >> (gpio_pin - 32)) & 1U);
}

static int wait_for_level(int gpio_pin, int level, unsigned int timeout_us)
{
    uint64_t start_time = get_cycles();

    while (gpio_read(gpio_pin) != level) {
        uint64_t current_cycles = get_cycles();
        if (elapsed_us(start_time, current_cycles) > (uint64_t)timeout_us) {
            return -1;
        }
    }

    return 0;
}

int dht11_init(dht11_t *sensor, int gpio_pin)
{
    if (sensor == NULL || gpio_pin < 0) {
        errno = EINVAL;
        return DHT11_ERROR;
    }

    memset(sensor, 0, sizeof(*sensor));
    g_gpio = gpio_pin;

    if (ThreadCtl(_NTO_TCTL_IO_PRIV, 0) == -1) {
        fprintf(stderr, "[DHT11] ThreadCtl failed: %s\n", strerror(errno));
        return DHT11_ERROR;
    }

    g_gpio_base = mmap_device_memory(
        NULL,
        GPIO_MAP_SIZE,
        PROT_READ | PROT_WRITE | PROT_NOCACHE,
        0,
        BCM2711_GPIO_BASE);

    if (g_gpio_base == MAP_FAILED) {
        g_gpio_base = NULL;
        fprintf(stderr, "[DHT11] GPIO mmap failed: %s\n", strerror(errno));
        return DHT11_ERROR;
    }

    g_cycles_per_second = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    if (g_cycles_per_second == 0ULL) {
        fprintf(stderr, "[DHT11] invalid ClockCycles frequency\n");
        dht11_close(sensor);
        return DHT11_ERROR;
    }

    gpio_set_input(g_gpio);
    sensor->valid = false;
    sensor->last_ts_ns = 0ULL;

    printf("[DHT11] initialized on GPIO%d\n", g_gpio);
    return DHT11_OK;
}

int dht11_read(dht11_t *sensor, uint64_t sample_time_ns)
{
    uint8_t bytes[5] = {0U, 0U, 0U, 0U, 0U};

    if (sensor == NULL || g_gpio_base == NULL) {
        errno = EINVAL;
        return DHT11_ERROR;
    }

    gpio_set_output(g_gpio);
    gpio_set_low(g_gpio);
    usleep(20000U);

    gpio_set_high(g_gpio);
    delay_us(30U);
    gpio_set_input(g_gpio);

    /* DHT11 response: LOW ~80 us, HIGH ~80 us, then data starts. */
    if (wait_for_level(g_gpio, 0, 120U) != 0 ||
        wait_for_level(g_gpio, 1, 120U) != 0 ||
        wait_for_level(g_gpio, 0, 120U) != 0) {
        sensor->valid = false;
        return DHT11_TIMEOUT;
    }

    for (int byte_index = 0; byte_index < 5; ++byte_index) {
        for (int bit_index = 7; bit_index >= 0; --bit_index) {
            uint64_t start_time;
            uint64_t end_time;
            uint64_t high_time;

            if (wait_for_level(g_gpio, 1, 100U) != 0) {
                sensor->valid = false;
                return DHT11_TIMEOUT;
            }

            start_time = get_cycles();
            if (wait_for_level(g_gpio, 0, 100U) != 0) {
                sensor->valid = false;
                return DHT11_TIMEOUT;
            }
            end_time = get_cycles();

            high_time = elapsed_us(start_time, end_time);
            /* DHT11 encodes 0 with ~26-28 us HIGH and 1 with ~70 us.
             * The 50 us midpoint gives margin for scheduler jitter. */
            if (high_time > 50ULL) {
                bytes[byte_index] |= (uint8_t)(1U << bit_index);
            }
        }
    }

    if ((uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]) != bytes[4]) {
        sensor->valid = false;
        fprintf(stderr,
                "[DHT11] checksum error: %02X %02X %02X %02X %02X\n",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);
        return DHT11_CHECKSUM;
    }

    sensor->humidity_pct = (int)bytes[0];
    sensor->temperature_c = (int)bytes[2];
    sensor->last_ts_ns = sample_time_ns;
    sensor->valid = true;

    return DHT11_OK;
}

void dht11_close(dht11_t *sensor)
{
    if (sensor != NULL) {
        sensor->valid = false;
        sensor->last_ts_ns = 0ULL;
    }

    if (g_gpio_base != NULL) {
        (void)munmap_device_memory((void *)g_gpio_base, GPIO_MAP_SIZE);
        g_gpio_base = NULL;
    }

    g_cycles_per_second = 0ULL;
}
