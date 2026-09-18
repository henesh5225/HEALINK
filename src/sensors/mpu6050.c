#include "mpu6050.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include "healink_config.h"

/* MPU6050-family register map used by the current breakout. */
#define REG_SMPLRT_DIV       0x19u
#define REG_CONFIG           0x1Au
#define REG_ACCEL_CONFIG     0x1Cu
#define REG_ACCEL_XOUT_H     0x3Bu
#define REG_PWR_MGMT_1      0x6Bu
#define REG_PWR_MGMT_2      0x6Cu
#define REG_WHO_AM_I        0x75u

#define MPU6050_WHO_AM_I_6050  0x68u
#define MPU6050_WHO_AM_I_6500  0x70u

/* Configuration used by HEALINK: 50 Hz sample stream, +/-2 g. */
#define MPU6050_CONFIG_DLPF       0x03u /* DLPF_CFG=3, accel BW ~44 Hz */
#define MPU6050_SAMPLE_DIV        19u   /* 1 kHz internal rate / (19+1) */
#define MPU6050_ACCEL_FS_2G       0x00u
#define MPU6050_PWR_ACTIVE        0x00u
#define MPU6050_PWR2_ALL_ON       0x00u
#define MPU6050_ACCEL_LSB_PER_G   16384.0f

/* Fall detector thresholds. These remain conservative and configurable. */
#define FALL_IMPACT_G             2.5f
#define FALL_RECOVERY_G           1.15f
#define FALL_IMPACT_SAMPLES       2U
#define FALL_RECOVERY_SAMPLES     2U
#define FALL_COOLDOWN_NS          3000000000ULL

static int16_t be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8U) | (uint16_t)p[1]);
}

static int write_checked(
    healink_i2c_t *i2c_bus,
    uint8_t i2c_address,
    uint8_t reg,
    uint8_t value)
{
    uint8_t readback;

    if (healink_i2c_write_reg8(
            i2c_bus, i2c_address, reg, value) != 0) {
        return -1;
    }

    if (healink_i2c_read_reg8(
            i2c_bus, i2c_address, reg, &readback) != 0) {
        return -1;
    }

    if (readback != value) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int mpu6050_init(
    mpu6050_t *sensor,
    healink_i2c_t *i2c_bus,
    uint8_t i2c_address)
{
    uint8_t who;
    uint8_t pwr2;

    if (sensor == NULL ||
        i2c_bus == NULL ||
        i2c_bus->file_descriptor < 0 ||
        i2c_address != HEALINK_MPU6050_ADDR) {
        errno = EINVAL;
        return -1;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->i2c_bus = i2c_bus;
    sensor->i2c_address = i2c_address;
    sensor->who_am_i = 0U;

    /* Accept standard MPU6050 (0x68) and MPU6500-family (0x70) identities. */
    if (healink_i2c_read_reg8(
            i2c_bus,
            i2c_address,
            REG_WHO_AM_I,
            &who) != 0) {
        return -1;
    }

    if (who != MPU6050_WHO_AM_I_6050 &&
        who != MPU6050_WHO_AM_I_6500) {
        errno = ENODEV;
        return -1;
    }

    sensor->who_am_i = who;

    /* Wake the device and ensure all sensor axes are enabled. */
    if (write_checked(
            i2c_bus,
            i2c_address,
            REG_PWR_MGMT_1,
            MPU6050_PWR_ACTIVE) != 0) {
        return -1;
    }

    if (write_checked(
            i2c_bus,
            i2c_address,
            REG_PWR_MGMT_2,
            MPU6050_PWR2_ALL_ON) != 0) {
        return -1;
    }

    /* Configure accelerometer DLPF, sample divider and +/-2g range. */
    if (write_checked(
            i2c_bus,
            i2c_address,
            REG_CONFIG,
            MPU6050_CONFIG_DLPF) != 0) {
        return -1;
    }

    if (write_checked(
            i2c_bus,
            i2c_address,
            REG_SMPLRT_DIV,
            MPU6050_SAMPLE_DIV) != 0) {
        return -1;
    }

    if (write_checked(
            i2c_bus,
            i2c_address,
            REG_ACCEL_CONFIG,
            MPU6050_ACCEL_FS_2G) != 0) {
        return -1;
    }

    /* Final wake-state check catches a device that ACKs but remains asleep. */
    if (healink_i2c_read_reg8(
            i2c_bus,
            i2c_address,
            REG_PWR_MGMT_2,
            &pwr2) != 0) {
        return -1;
    }

    if (pwr2 != MPU6050_PWR2_ALL_ON) {
        errno = EIO;
        return -1;
    }

    sensor->valid = false;
    sensor->fall_event = false;
    sensor->impact_count = 0U;
    sensor->recovery_count = 0U;
    sensor->impact_start_ns = 0ULL;
    sensor->last_fall_detect_ns = 0ULL;
    sensor->last_ts_ns = 0ULL;

    return 0;
}

int mpu6050_read(
    mpu6050_t *sensor,
    uint64_t sample_time_ns)
{
    uint8_t data[14];
    int16_t ax;
    int16_t ay;
    int16_t az;
    float mag;

    if (sensor == NULL ||
        sensor->i2c_bus == NULL ||
        sensor->i2c_bus->file_descriptor < 0) {
        errno = EINVAL;
        return -1;
    }

    sensor->valid = false;
    sensor->fall_event = false;

    if (healink_i2c_read_regs(
            sensor->i2c_bus,
            sensor->i2c_address,
            REG_ACCEL_XOUT_H,
            data,
            sizeof(data)) != 0) {
        sensor->impact_count = 0U;
        sensor->recovery_count = 0U;
        sensor->impact_start_ns = 0ULL;
        return -1;
    }

    ax = be16(&data[0]);
    ay = be16(&data[2]);
    az = be16(&data[4]);

    sensor->ax_g = (float)ax / MPU6050_ACCEL_LSB_PER_G;
    sensor->ay_g = (float)ay / MPU6050_ACCEL_LSB_PER_G;
    sensor->az_g = (float)az / MPU6050_ACCEL_LSB_PER_G;

    mag = sqrtf(
        sensor->ax_g * sensor->ax_g +
        sensor->ay_g * sensor->ay_g +
        sensor->az_g * sensor->az_g);

    if (!isfinite(mag) || mag > 16.0f) {
        errno = ERANGE;
        sensor->impact_count = 0U;
        sensor->recovery_count = 0U;
        sensor->impact_start_ns = 0ULL;
        return -1;
    }

    sensor->mag_g = mag;

    /*
     * Bounded impact/recovery fall candidate detector.
     * The acquisition path remains polling at 50 Hz. GPIO23 interrupt
     * integration stays a separate later phase.
     */
    if (mag > FALL_IMPACT_G) {
        if (sensor->impact_count == 0U) {
            sensor->impact_start_ns = sample_time_ns;
        }

        if (sensor->impact_count < FALL_IMPACT_SAMPLES) {
            ++sensor->impact_count;
        }

        sensor->recovery_count = 0U;

    } else if (sensor->impact_count > 0U &&
               sensor->impact_start_ns != 0ULL &&
               sample_time_ns >= sensor->impact_start_ns &&
               (sample_time_ns - sensor->impact_start_ns) > HEALINK_FALL_CANDIDATE_NS) {

        sensor->impact_count = 0U;
        sensor->recovery_count = 0U;
        sensor->impact_start_ns = 0ULL;

    } else if (sensor->impact_count >= FALL_IMPACT_SAMPLES &&
               mag < FALL_RECOVERY_G) {

        if (sensor->recovery_count < FALL_RECOVERY_SAMPLES) {
            ++sensor->recovery_count;
        }

        if (sensor->recovery_count >= FALL_RECOVERY_SAMPLES &&
            (sensor->last_fall_detect_ns == 0ULL ||
             sample_time_ns >= sensor->last_fall_detect_ns + FALL_COOLDOWN_NS)) {

            sensor->fall_event = true;
            sensor->last_fall_detect_ns = sample_time_ns;
            sensor->impact_count = 0U;
            sensor->recovery_count = 0U;
            sensor->impact_start_ns = 0ULL;
        }

    } else if (mag >= FALL_RECOVERY_G) {
        sensor->recovery_count = 0U;

        if (mag <= FALL_IMPACT_G && sensor->impact_count > 0U) {
            --sensor->impact_count;

            if (sensor->impact_count == 0U) {
                sensor->impact_start_ns = 0ULL;
            }
        }
    }

    sensor->valid = true;
    sensor->last_ts_ns = sample_time_ns;

    return 0;
}
