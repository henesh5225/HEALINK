#include "healink_config.h"
#include "qnx_i2c.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int probe_reg8(healink_i2c_t *bus,
                      const char *name,
                      uint8_t address,
                      uint8_t reg,
                      uint8_t expected_a,
                      uint8_t expected_b)
{
    uint8_t value = 0U;

    if (healink_i2c_read_reg8(bus, address, reg, &value) != 0) {
        fprintf(stderr, "%-10s 0x%02X  NOT FOUND (%s)\n",
                name, address, strerror(errno));
        return -1;
    }

    if (value != expected_a && value != expected_b) {
        fprintf(stderr, "%-10s 0x%02X  UNEXPECTED ID 0x%02X\n",
                name, address, value);
        return -1;
    }

    printf("%-10s 0x%02X  FOUND (ID 0x%02X)\n", name, address, value);
    return 0;
}

static int probe_ads1115(healink_i2c_t *bus)
{
    uint16_t config = 0U;

    if (healink_i2c_read_reg16_be(bus, HEALINK_ADS1115_ADDR,
                                   0x01U, &config) != 0) {
        fprintf(stderr, "%-10s 0x%02X  NOT FOUND (%s)\n",
                "ADS1115", HEALINK_ADS1115_ADDR, strerror(errno));
        return -1;
    }

    printf("%-10s 0x%02X  FOUND (CONFIG 0x%04X)\n",
           "ADS1115", HEALINK_ADS1115_ADDR, (unsigned)config);
    return 0;
}

static int probe_ssd1306(healink_i2c_t *bus)
{
    /* SSD1306 has no identity register.  0xE3 is its documented no-op command. */
    const uint8_t nop[] = {0x00U, 0xE3U};

    if (healink_i2c_write(bus, HEALINK_OLED_I2C_ADDR, nop, sizeof(nop)) != 0) {
        fprintf(stderr, "%-10s 0x%02X  NOT FOUND (%s)\n",
                "SSD1306", HEALINK_OLED_I2C_ADDR, strerror(errno));
        return -1;
    }

    printf("%-10s 0x%02X  ACKNOWLEDGED (no identity register)\n",
           "SSD1306", HEALINK_OLED_I2C_ADDR);
    return 0;
}

int main(void)
{
    healink_i2c_t bus = { -1, 0U };
    int failures = 0;

    printf("HEALINK QNX I2C diagnostic\n");
    printf("Bus: %s at 400000 Hz\n\n", HEALINK_I2C_DEV);

    if (healink_i2c_open(&bus, HEALINK_I2C_DEV, 400000U) != 0) {
        fprintf(stderr, "I2C BUS   %s  FAILED (%s)\n",
                HEALINK_I2C_DEV, strerror(errno));
        return 1;
    }

    printf("I2C BUS   %s  OK\n\n", HEALINK_I2C_DEV);
    failures += probe_reg8(&bus, "MAX30102", HEALINK_MAX30102_ADDR,
                           0xFFU, 0x15U, 0x15U) != 0;
    /* MPU6050-family identity: 0x68 for MPU6050, 0x70 for MPU6500. */
    failures += probe_reg8(&bus, "MPU6050", HEALINK_MPU6050_ADDR,
                           0x75U, 0x68U, 0x70U) != 0;
    failures += probe_ads1115(&bus) != 0;
    failures += probe_ssd1306(&bus) != 0;

    healink_i2c_close(&bus);

    if (failures != 0) {
        fprintf(stderr, "\nDiagnostic completed with %d failed probe(s).\n", failures);
        return 1;
    }

    puts("\nAll configured I2C devices responded as expected.");
    return 0;
}
