#include "ds18b20.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/neutrino.h>
#include <time.h>

#include "healink_config.h"

/*
 * BCM2711 GPIO register block used for GPIO4:
 *   GPFSEL0  0x000
 *   GPSET0   0x01C
 *   GPCLR0   0x028
 *   GPLEV0   0x034
 *
 * The DATA line is operated as an open-drain style signal:
 *   output-low = pull the 1-Wire bus LOW
 *   input      = release the bus; external 4.7 kOhm pull-up makes it HIGH
 *
 * We intentionally do not invoke gpio_pin-bcm2711 for each slot because its
 * command/resource-manager path is not a microsecond 1-Wire slot primitive.
 */

#define BCM2711_GPIO_BASE      UINT64_C(0xFE200000)
#define GPIO_MAP_SIZE          0x1000U

#define GPFSEL0_OFFSET         0x000U
#define GPSET0_OFFSET          0x01CU
#define GPCLR0_OFFSET          0x028U
#define GPLEV0_OFFSET          0x034U

#define GPIO_INPUT             0U
#define GPIO_OUTPUT            1U

/* 1-Wire timing, expressed in nanoseconds. */
#define OW_RESET_LOW_NS        480000ULL
#define OW_PRESENCE_WAIT_NS     40000ULL
#define OW_RESET_RECOVERY_NS   410000ULL
#define OW_SLOT_NS              70000ULL

#define OW_WRITE1_LOW_NS         6000ULL
#define OW_WRITE0_LOW_NS        60000ULL

#define OW_READ_LOW_NS           6000ULL
#define OW_READ_SAMPLE_NS        9000ULL

#define DS18B20_SKIP_ROM        0xCCU
#define DS18B20_CONVERT_T       0x44U
#define DS18B20_READ_SCRATCHPAD 0xBEU

typedef struct {
    volatile uint8_t *base;
    int gpio_pin;
    unsigned bit_value;
    unsigned fsel_shift;
    bool mapped;
} ds18b20_bus_t;

static ds18b20_bus_t g_bus;

static void delay_ns(uint64_t ns)
{
    /*
     * QNX documents nanospin_ns() specifically for short hardware delays.
     * Every 1-Wire reset/slot timing interval is below 1 ms, so keep those
     * intervals in a busy-wait rather than allowing the scheduler to insert
     * millisecond-scale jitter. The 750 ms temperature conversion is handled
     * separately below with a POSIX sleep.
     */
    if (ns == 0ULL) {
        return;
    }

    if (ns <= 500000000ULL) {
        (void)nanospin_ns((unsigned long)ns);
        return;
    }

    {
        struct timespec req;
        struct timespec rem;

        req.tv_sec = (time_t)(ns / UINT64_C(1000000000));
        req.tv_nsec = (long)(ns % UINT64_C(1000000000));

        while (nanosleep(&req, &rem) != 0) {
            if (errno != EINTR) {
                break;
            }
            req = rem;
        }
    }
}

static uint32_t mmio_read32(unsigned offset)
{
    volatile uint32_t *p =
        (volatile uint32_t *)(void *)(g_bus.base + offset);

    return *p;
}

static void mmio_write32(unsigned offset, uint32_t byte_value)
{
    volatile uint32_t *p =
        (volatile uint32_t *)(void *)(g_bus.base + offset);

    *p = byte_value;
}

static int set_direction(unsigned direction)
{
    uint32_t reg;
    uint32_t mask;

    if (!g_bus.mapped ||
        g_bus.gpio_pin < 0 ||
        g_bus.gpio_pin > 31 ||
        (direction != GPIO_INPUT && direction != GPIO_OUTPUT)) {
        errno = EINVAL;
        return -1;
    }

    mask = UINT32_C(7) << g_bus.fsel_shift;
    reg = mmio_read32(GPFSEL0_OFFSET);
    reg &= ~mask;

    if (direction == GPIO_OUTPUT) {
        reg |= UINT32_C(1) << g_bus.fsel_shift;
    }

    mmio_write32(GPFSEL0_OFFSET, reg);
    return 0;
}

static int read_level(void)
{
    uint32_t reg;

    if (!g_bus.mapped) {
        errno = EINVAL;
        return -1;
    }

    reg = mmio_read32(GPLEV0_OFFSET);

    return (reg & (UINT32_C(1) << g_bus.bit_value)) != 0U ? 1 : 0;
}

static void drive_low(void)
{
    mmio_write32(
        GPCLR0_OFFSET,
        UINT32_C(1) << g_bus.bit_value);
}

static int begin_low(void)
{
    /*
     * Make sure the output latch is LOW before selecting output mode.
     * This avoids a HIGH glitch when changing the function selector.
     */
    drive_low();
    return set_direction(GPIO_OUTPUT);
}

static int release_bus(void)
{
    return set_direction(GPIO_INPUT);
}

static bool one_wire_reset(void)
{
    int level;

    if (begin_low() != 0) {
        return false;
    }

    delay_ns(OW_RESET_LOW_NS);

    if (release_bus() != 0) {
        return false;
    }

    delay_ns(OW_PRESENCE_WAIT_NS);

    level = read_level();

    delay_ns(OW_RESET_RECOVERY_NS);

    /*
     * DS18B20 presence is represented by the slave pulling the bus LOW.
     */
    return level == 0;
}

static int one_wire_write_bit(unsigned bit_value)
{
    if (begin_low() != 0) {
        return -1;
    }

    if (bit_value != 0U) {
        delay_ns(OW_WRITE1_LOW_NS);

        if (release_bus() != 0) {
            return -1;
        }

        delay_ns(OW_SLOT_NS - OW_WRITE1_LOW_NS);
    } else {
        delay_ns(OW_WRITE0_LOW_NS);

        if (release_bus() != 0) {
            return -1;
        }

        delay_ns(OW_SLOT_NS - OW_WRITE0_LOW_NS);
    }

    return 0;
}

static int one_wire_read_bit(unsigned *bit_value)
{
    int level;

    if (bit_value == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (begin_low() != 0) {
        return -1;
    }

    delay_ns(OW_READ_LOW_NS);

    if (release_bus() != 0) {
        return -1;
    }

    delay_ns(OW_READ_SAMPLE_NS);

    level = read_level();
    if (level < 0) {
        return -1;
    }

    *bit_value = (unsigned)level;

    delay_ns(
        OW_SLOT_NS -
        OW_READ_LOW_NS -
        OW_READ_SAMPLE_NS);

    return 0;
}

static int one_wire_write_byte(uint8_t byte_value)
{
    unsigned i;

    for (i = 0U; i < 8U; ++i) {
        if (one_wire_write_bit(
                (unsigned)(byte_value & 0x01U)) != 0) {
            return -1;
        }

        byte_value >>= 1;
    }

    return 0;
}

static int one_wire_read_byte(uint8_t *byte_value)
{
    uint8_t result = 0U;
    unsigned i;

    if (byte_value == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0U; i < 8U; ++i) {
        unsigned bit_value;

        if (one_wire_read_bit(&bit_value) != 0) {
            return -1;
        }

        result |= (uint8_t)(bit_value << i);
    }

    *byte_value = result;
    return 0;
}

static uint8_t crc8(
    const uint8_t *data,
    size_t len)
{
    uint8_t crc = 0U;
    size_t i;

    for (i = 0U; i < len; ++i) {
        uint8_t byte = data[i];
        unsigned j;

        for (j = 0U; j < 8U; ++j) {
            uint8_t mix =
                (uint8_t)((crc ^ byte) & 0x01U);

            crc >>= 1;

            if (mix != 0U) {
                crc ^= 0x8CU;
            }

            byte >>= 1;
        }
    }

    return crc;
}

static int map_gpio(void)
{
    void *mapped;

    if (g_bus.mapped) {
        return 0;
    }

    if (ThreadCtl(_NTO_TCTL_IO_PRIV, 0) == -1) {
        return -1;
    }

    mapped = mmap_device_memory(
        NULL,
        GPIO_MAP_SIZE,
        PROT_READ | PROT_WRITE | PROT_NOCACHE,
        0,
        BCM2711_GPIO_BASE);

    if (mapped == MAP_FAILED) {
        return -1;
    }

    g_bus.base = (volatile uint8_t *)mapped;
    g_bus.mapped = true;
    return 0;
}

static void unmap_gpio(void)
{
    if (g_bus.mapped && g_bus.base != NULL) {
        (void)munmap_device_memory(
            (void *)g_bus.base,
            GPIO_MAP_SIZE);
    }

    memset(&g_bus, 0, sizeof(g_bus));
}

static int validate_temperature(float temperature_c)
{
    if (temperature_c < HEALINK_MIN_TEMP_C ||
        temperature_c > HEALINK_MAX_TEMP_C) {
        errno = ERANGE;
        return -1;
    }

    return 0;
}

int ds18b20_init(ds18b20_t *sensor, int gpio_pin)
{
    if (sensor == NULL || gpio_pin != HEALINK_GPIO_DS18B20) {
        errno = EINVAL;
        return DS18B20_ERROR;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->gpio_pin = gpio_pin;

    if (map_gpio() != 0) {
        return DS18B20_ERROR;
    }

    g_bus.gpio_pin = gpio_pin;
    g_bus.bit_value = (unsigned)gpio_pin;
    g_bus.fsel_shift = (unsigned)gpio_pin * 3U;

    if (release_bus() != 0) {
        unmap_gpio();
        return DS18B20_ERROR;
    }

    if (!one_wire_reset()) {
        unmap_gpio();
        errno = ENODEV;
        return DS18B20_NO_SENSOR;
    }

    return DS18B20_OK;
}

int ds18b20_read(
    ds18b20_t *sensor,
    uint64_t now_ns)
{
    uint8_t scratchpad[9];
    uint16_t raw;
    int16_t signed_raw;
    float temperature_c;
    unsigned i;

    if (sensor == NULL || !g_bus.mapped) {
        errno = EINVAL;
        return DS18B20_ERROR;
    }

    sensor->valid = false;

    if (!one_wire_reset()) {
        errno = ENODEV;
        return DS18B20_NO_SENSOR;
    }

    if (one_wire_write_byte(DS18B20_SKIP_ROM) != 0 ||
        one_wire_write_byte(DS18B20_CONVERT_T) != 0) {
        return DS18B20_ERROR;
    }

    /*
     * 12-bit_value conversion. The DS18B20 conversion interval can be up to
     * approximately 750 ms. This wait occurs only in the low-priority
     * DS18B20 worker; fusion, safety and UI remain separate threads.
     */
    delay_ns(750000000ULL);

    if (!one_wire_reset()) {
        errno = ENODEV;
        return DS18B20_NO_SENSOR;
    }

    if (one_wire_write_byte(DS18B20_SKIP_ROM) != 0 ||
        one_wire_write_byte(DS18B20_READ_SCRATCHPAD) != 0) {
        return DS18B20_ERROR;
    }

    for (i = 0U; i < 9U; ++i) {
        if (one_wire_read_byte(&scratchpad[i]) != 0) {
            return DS18B20_ERROR;
        }
    }

    if (crc8(scratchpad, 8U) != scratchpad[8]) {
        errno = EPROTO;
        return DS18B20_CRC_ERROR;
    }

    raw = (uint16_t)scratchpad[0] |
          (uint16_t)((uint16_t)scratchpad[1] << 8);

    signed_raw = (int16_t)raw;
    temperature_c = (float)signed_raw / 16.0f;

    if (validate_temperature(temperature_c) != 0) {
        return DS18B20_INVALID;
    }

    sensor->temperature_c = temperature_c;
    sensor->valid = true;
    sensor->last_ts_ns = now_ns;

    return DS18B20_OK;
}
