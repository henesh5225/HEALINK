#ifndef HEALINK_CONFIG_H
#define HEALINK_CONFIG_H

/* QNX target / device paths. These are configurable without touching logic. */
#define HEALINK_I2C_DEV              "/dev/i2c1"
#define HEALINK_GPIO_DEV             "/dev/gpio"
#define HEALINK_OLED_I2C_ADDR        0x3Cu
#define HEALINK_MAX30102_ADDR        0x57u
#define HEALINK_MPU6050_ADDR         0x68u
#define HEALINK_ADS1115_ADDR         0x48u

/* Raspberry Pi BCM GPIO numbering. */
#define HEALINK_GPIO_DS18B20         4
#define HEALINK_GPIO_DHT11           22
#define HEALINK_GPIO_MPU6050_INT     23
#define HEALINK_GPIO_SOS_BUTTON       24

/*
 * Optional AD8232 lead-off GPIOs. Set these to the actual BCM GPIO
 * numbers used by the selected AD8232 breakout before enabling lead-off
 * detection. -1 means the board pinout has not been assigned yet; the
 * ECG acquisition path remains usable, but lead status is reported as
 * unavailable rather than being guessed.
 */
#define HEALINK_GPIO_AD8232_LOD_MINUS -1
#define HEALINK_GPIO_AD8232_LOD_PLUS  -1

/* Sensor periods in nanoseconds. */
#define HEALINK_PERIOD_MAX30102_NS   10000000ULL   /* 10 ms / 100 Hz */
#define HEALINK_PERIOD_AD8232_NS     4000000ULL    /* 4 ms / 250 Hz ADC */
#define HEALINK_PERIOD_MPU6050_NS    20000000ULL   /* 20 ms / 50 Hz */
#define HEALINK_PERIOD_DS18B20_NS    1000000000ULL /* 1 s */
#define HEALINK_PERIOD_DHT11_NS      2000000000ULL /* 2 s */
#define HEALINK_PERIOD_FUSION_NS     50000000ULL   /* 50 ms / 20 Hz */
#define HEALINK_PERIOD_STALE_NS      100000000ULL  /* 100 ms */
#define HEALINK_FALL_LATCH_NS        1500000000ULL /* 1.5 s */
#define HEALINK_FALL_CANDIDATE_NS    200000000ULL  /* 200 ms */
#define HEALINK_FALL_COOLDOWN_NS     3000000000ULL /* 3 s */
#define HEALINK_DISTRESS_CONFIRM_CYCLES 3U          /* 150 ms */
#define HEALINK_RECOVERY_CONFIRM_CYCLES 5U          /* 250 ms */

/* Staleness margins. Per-sensor check is base period * margin. */
#define HEALINK_STALE_MARGIN_NUM     3ULL
#define HEALINK_STALE_MARGIN_DEN     2ULL

/* Data-quality limits. */
#define HEALINK_MAX_HR_BPM           240.0f
#define HEALINK_MIN_HR_BPM           30.0f
#define HEALINK_MIN_SPO2_PCT         70.0f
#define HEALINK_MAX_SPO2_PCT         100.0f
#define HEALINK_MIN_TEMP_C           25.0f
#define HEALINK_MAX_TEMP_C           45.0f
#define HEALINK_ECG_PPG_MAX_DIFF_BPM 15.0f

/* Confidence coverage policy. Overall confidence is capped when the
 * physiological core (HR and SpO2) is only partially observed or absent.
 * This prevents a single valid temperature/motion channel from being
 * presented as full health-assessment confidence. */
#define HEALINK_CONF_MAX_NO_PHYSIOLOGY 0.25f
#define HEALINK_CONF_MAX_SINGLE_PHYSIOLOGY 0.65f
#define HEALINK_CONF_PHYSIOLOGY_THRESHOLD 0.35f

/* QNX priorities. Keep emergency path above fusion, sensors and UI. */
#define HEALINK_PRIO_EMERGENCY       60
#define HEALINK_PRIO_FUSION          45
#define HEALINK_PRIO_MAX30102        30
#define HEALINK_PRIO_AD8232          30
#define HEALINK_PRIO_MPU6050         35
#define HEALINK_PRIO_DS18B20         20
#define HEALINK_PRIO_DHT11            18
#define HEALINK_PRIO_UI              15
#define HEALINK_PRIO_SOS_BUTTON       55
#define HEALINK_PRIO_LORA              50

/* ADS1115 conversion rate for AD8232 analog output. */
#define HEALINK_ADS1115_DATA_RATE_SPS 860

/* MAX30102 FIFO processing. */
#define HEALINK_MAX30102_WINDOW       300

/* Absolute optical DC thresholds used only for finger-presence gating. */
#define HEALINK_MAX30102_FINGER_IR_DC_MIN  5000.0f
#define HEALINK_MAX30102_FINGER_RED_DC_MIN 3000.0f

/* No fabricated data. Missing hardware remains INVALID/STALE. */
#define HEALINK_ALLOW_SYNTHETIC_DATA  0

#endif
