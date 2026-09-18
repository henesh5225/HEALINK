#ifndef HEALINK_TYPES_H
#define HEALINK_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/*
 * ============================================================
 * HEALINK - SENSOR MASKS
 * ============================================================
 *
 * These masks identify the four health sensors used by the
 * PS#24 health-fusion pipeline.
 *
 * Bit 0 : MAX30102  - PPG / SpO2
 * Bit 1 : AD8232    - ECG via ADS1115
 * Bit 2 : MPU6050   - motion / acceleration
 * Bit 3 : DS18B20   - body/contact temperature
 * Bit 4 : DHT11      - ambient temperature / humidity
 */

#define SENSOR_MAX30102  (1u << 0)
#define SENSOR_AD8232    (1u << 1)
#define SENSOR_MPU6050   (1u << 2)
#define SENSOR_DS18B20   (1u << 3)
#define SENSOR_DHT11      (1u << 4)

#define SENSOR_ALL_HEALTH \
    (SENSOR_MAX30102 | SENSOR_AD8232 | \
     SENSOR_MPU6050 | SENSOR_DS18B20)

/*
 * ============================================================
 * EVENT TYPES
 * ============================================================
 */

#define EVENT_SENSOR_FAULT    1u
#define EVENT_STALE_DATA      2u
#define EVENT_FALL_DETECTED   3u
#define EVENT_LOW_BATTERY     4u
#define EVENT_SOS             5u
#define EVENT_ALERT           6u
#define EVENT_RECOVERY        7u
#define EVENT_SHUTDOWN        8u
#define EVENT_EMERGENCY       9u
#define EVENT_DISTRESS       10u
#define EVENT_SENSOR_STALE   11u
#define EVENT_INCONSISTENT   12u
#define EVENT_STATE_CHANGE   13u
#define EVENT_LORA_ACK       14u
#define EVENT_LORA_FAILURE   15u

/*
 * ============================================================
 * HEALTH STATE
 * ============================================================
 */

typedef enum {
    HEALTH_UNKNOWN = 0,
    HEALTH_NORMAL,
    HEALTH_RESTING,
    HEALTH_ACTIVE,
    HEALTH_STRESSED,
    HEALTH_DISTRESS,
    HEALTH_FALL,
    HEALTH_SENSOR_UNCERTAIN
} healink_health_state_t;

/*
 * ============================================================
 * ACTIVITY
 * ============================================================
 */

typedef enum {
    ACTIVITY_UNKNOWN = 0,
    ACTIVITY_RESTING,
    ACTIVITY_ACTIVE,
    ACTIVITY_FALL
} healink_activity_t;

/*
 * ============================================================
 * SENSOR / FUSION STATE
 * ============================================================
 */

typedef struct {

    /*
     * --------------------------------------------------------
     * Primary health measurements
     * --------------------------------------------------------
     */

    float hr_bpm;
    float ecg_hr_bpm;
    float ppg_hr_bpm;
    float spo2_pct;
    float temperature_c;
    float ambient_temperature_c;
    float humidity_pct;

    /*
     * --------------------------------------------------------
     * Motion
     * --------------------------------------------------------
     */

    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float accel_mag_g;

    /*
     * --------------------------------------------------------
     * Per-source confidence
     * --------------------------------------------------------
     */

    float hr_confidence;
    float ecg_quality;
    float ppg_quality;
    float hr_stability;
    float previous_hr_bpm;
    bool hr_history_valid;
    float spo2_confidence;
    float temperature_confidence;
    float motion_confidence;

    /*
     * --------------------------------------------------------
     * ECG / PPG cross-validation
     * --------------------------------------------------------
     */

    float ecg_ppg_delta_bpm;
    float ecg_ppg_confidence;

    /*
     * --------------------------------------------------------
     * Overall fusion confidence
     * --------------------------------------------------------
     */

    float overall_confidence;
    float anomaly_score;
    float fall_confidence;

    /*
     * --------------------------------------------------------
     * Data-quality masks
     * --------------------------------------------------------
     *
     * VALID:
     *     The sensor currently has usable data.
     *
     * STALE:
     *     The sensor has not produced a fresh timestamp within
     *     its expected temporal deadline.
     *
     * OFFLINE:
     *     The sensor could not be brought online during
     *     initialization. This is NOT treated as a runtime
     *     hardware fault.
     *
     * FAULT:
     *     A sensor that was expected to be operational
     *     experienced a runtime communication/read failure.
     *
     * INCONSISTENT:
     *     A valid sensor conflicts with another valid source.
     */

    uint32_t valid_mask;
    uint32_t stale_mask;
    uint32_t offline_mask;
    uint32_t fault_mask;
    uint32_t inconsistent_mask;

    /*
     * --------------------------------------------------------
     * Sensor timestamps
     * --------------------------------------------------------
     */

    uint64_t max30102_ts_ns;
    uint64_t ad8232_ts_ns;
    uint64_t mpu6050_ts_ns;
    uint64_t ds18b20_ts_ns;
    uint64_t dht11_ts_ns;

    /*
     * --------------------------------------------------------
     * Classification
     * --------------------------------------------------------
     */

    healink_health_state_t health_state;
    healink_activity_t activity;

    /*
     * --------------------------------------------------------
     * Safety
     * --------------------------------------------------------
     */

    bool fall_event;
    uint64_t last_fall_ts_ns;

    /* A7 deterministic safety latches. */
    bool emergency_latched;
    bool alert_latched;
    uint8_t distress_confirm_count;
    uint8_t recovery_confirm_count;

} healink_sensor_state_t;

/*
 * ============================================================
 * EVENT MESSAGE
 * ============================================================
 *
 * Used by the QNX event queue for discrete events such as:
 *
 *     SENSOR_FAULT
 *     RECOVERY
 *     FALL_DETECTED
 *     DISTRESS
 *     INCONSISTENT
 *     STATE_CHANGE
 *     SHUTDOWN
 *     EMERGENCY
 *     DISTRESS
 *     ALERT
 */

typedef struct {

    uint32_t event_type;
    uint32_t sensor_mask;
    uint64_t timestamp_ns;

    float value_a;
    float value_b;

} healink_event_t;

#endif /* HEALINK_TYPES_H */
