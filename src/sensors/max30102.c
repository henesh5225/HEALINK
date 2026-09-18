#define _POSIX_C_SOURCE 200809L
#include "max30102.h"
#include "qnx_i2c.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define REG_INT_STATUS1   0x00u
#define REG_INT_STATUS2   0x01u
#define REG_INT_ENABLE1   0x02u
#define REG_INT_ENABLE2   0x03u
#define REG_FIFO_WR       0x04u
#define REG_FIFO_OVF      0x05u
#define REG_FIFO_RD       0x06u
#define REG_FIFO_DATA     0x07u
#define REG_FIFO_CONFIG   0x08u
#define REG_MODE_CONFIG   0x09u
#define REG_SPO2_CONFIG   0x0Au
#define REG_LED1_PA       0x0Cu
#define REG_LED2_PA       0x0Du
#define REG_PART_ID       0xFFu

#define MAX30102_SAMPLE_HZ 100.0f
#define MAX30102_WINDOW_REQUIRED 40U
#define MAX30102_FIFO_DEPTH 32U
#define MAX30102_MAX_DRAIN_PER_CYCLE 8U
#define MAX30102_SAMPLE_PERIOD_NS 10000000ULL

static float mean(const float *x, unsigned n)
{
    float sum = 0.0f;
    unsigned i;

    if (n == 0U) {
        return 0.0f;
    }

    for (i = 0U; i < n; ++i) {
        sum += x[i];
    }

    return sum / (float)n;
}

static float rms_ac(const float *x, unsigned n, float dc)
{
    float sum = 0.0f;
    unsigned i;

    if (n == 0U) {
        return 0.0f;
    }

    for (i = 0U; i < n; ++i) {
        const float d = x[i] - dc;
        sum += d * d;
    }

    return sqrtf(sum / (float)n);
}

static float detect_hr(const float *x, unsigned n, float sample_hz, float *beat_quality)
{
    float min_v;
    float max_v;
    float span;
    float threshold;
    float smooth[HEALINK_MAX30102_WINDOW];
    uint64_t peaks_idx[16];
    uint64_t intervals[15];
    unsigned peak_count = 0U;
    unsigned interval_count = 0U;
    unsigned i;

    if (beat_quality != NULL) {
        *beat_quality = 0.0f;
    }

    if (x == NULL || n < MAX30102_WINDOW_REQUIRED || sample_hz <= 0.0f) {
        return 0.0f;
    }

    min_v = x[0];
    max_v = x[0];
    for (i = 1U; i < n; ++i) {
        if (x[i] < min_v) min_v = x[i];
        if (x[i] > max_v) max_v = x[i];
    }

    span = max_v - min_v;
    if (span < 10.0f) {
        return 0.0f;
    }

    /* Five-sample moving average suppresses high-frequency spikes/noise. */
    for (i = 0U; i < n; ++i) {
        unsigned first = (i >= 2U) ? i - 2U : 0U;
        unsigned last = (i + 2U < n) ? i + 2U : n - 1U;
        float sum = 0.0f;
        unsigned count = 0U;
        unsigned j;

        for (j = first; j <= last; ++j) {
            sum += x[j];
            ++count;
        }
        smooth[i] = sum / (float)count;
    }

    /* Peak threshold is relative to the observed pulsatile range. */
    {
        float smin = smooth[0];
        float smax = smooth[0];
        for (i = 1U; i < n; ++i) {
            if (smooth[i] < smin) smin = smooth[i];
            if (smooth[i] > smax) smax = smooth[i];
        }
        if ((smax - smin) < 5.0f) {
            return 0.0f;
        }
        threshold = smin + 0.60f * (smax - smin);
    }

    /* 0.36 s minimum separation => rejects implausible double-counting. */
    {
        const uint64_t min_spacing = (uint64_t)(0.36f * sample_hz);
        for (i = 2U; i + 2U < n; ++i) {
            bool local_max =
                smooth[i] > threshold &&
                smooth[i] >= smooth[i - 1U] &&
                smooth[i] > smooth[i + 1U] &&
                smooth[i] >= smooth[i - 2U] &&
                smooth[i] > smooth[i + 2U];

            if (local_max) {
                if (peak_count == 0U ||
                    ((uint64_t)i - peaks_idx[peak_count - 1U]) >= min_spacing) {
                    if (peak_count < 16U) {
                        peaks_idx[peak_count++] = (uint64_t)i;
                    }
                }
            }
        }
    }

    if (peak_count < 3U) {
        return 0.0f;
    }

    for (i = 1U; i < peak_count; ++i) {
        const uint64_t interval = peaks_idx[i] - peaks_idx[i - 1U];
        if (interval >= (uint64_t)(0.36f * sample_hz) &&
            interval <= (uint64_t)(1.50f * sample_hz) &&
            interval_count < 15U) {
            intervals[interval_count++] = interval;
        }
    }

    if (interval_count < 2U) {
        return 0.0f;
    }

    /* Sort the small interval set and use its median to resist one bad beat. */
    for (i = 1U; i < interval_count; ++i) {
        uint64_t key = intervals[i];
        unsigned j = i;
        while (j > 0U && intervals[j - 1U] > key) {
            intervals[j] = intervals[j - 1U];
            --j;
        }
        intervals[j] = key;
    }

    {
        const uint64_t median_interval = intervals[interval_count / 2U];
        double mean_interval = 0.0;
        double deviation = 0.0;
        float consistency;

        for (i = 0U; i < interval_count; ++i) {
            mean_interval += (double)intervals[i];
        }
        mean_interval /= (double)interval_count;

        for (i = 0U; i < interval_count; ++i) {
            double d = (double)intervals[i] - mean_interval;
            deviation += d * d;
        }
        deviation = sqrt(deviation / (double)interval_count);

        consistency = (mean_interval > 0.0)
            ? (float)(1.0 - (deviation / mean_interval) / 0.35)
            : 0.0f;
        if (consistency < 0.0f) consistency = 0.0f;
        if (consistency > 1.0f) consistency = 1.0f;

        if (beat_quality != NULL) {
            float count_quality = (interval_count >= 5U) ? 1.0f : 0.75f;
            *beat_quality = consistency * count_quality;
        }

        if (consistency < 0.30f) {
            return 0.0f;
        }

        return 60.0f * sample_hz / (float)median_interval;
    }
}

static int max30102_write_reg(
    max30102_t *sensor,
    uint8_t reg,
    uint8_t value)
{
    return healink_i2c_write_reg8(
        sensor->i2c_bus,
        sensor->i2c_address,
        reg,
        value);
}

static int max30102_read_reg(
    max30102_t *sensor,
    uint8_t reg,
    uint8_t *value)
{
    return healink_i2c_read_reg8(
        sensor->i2c_bus,
        sensor->i2c_address,
        reg,
        value);
}

static int max30102_clear_fifo(max30102_t *sensor)
{
    if (max30102_write_reg(sensor, REG_FIFO_WR, 0x00u) != 0) {
        return -1;
    }

    if (max30102_write_reg(sensor, REG_FIFO_OVF, 0x00u) != 0) {
        return -1;
    }

    if (max30102_write_reg(sensor, REG_FIFO_RD, 0x00u) != 0) {
        return -1;
    }

    return 0;
}

static unsigned max30102_fifo_available(
    uint8_t write_ptr,
    uint8_t read_ptr)
{
    return (unsigned)((write_ptr - read_ptr) & 0x1Fu);
}

static int max30102_append_sample(
    max30102_t *sensor,
    uint32_t red,
    uint32_t ir)
{
    if (sensor->sample_count < HEALINK_MAX30102_WINDOW) {
        sensor->red_samples[sensor->sample_count] = (float)red;
        sensor->ir_samples[sensor->sample_count] = (float)ir;
        ++sensor->sample_count;
    } else {
        memmove(sensor->red_samples,
                sensor->red_samples + 1U,
                (HEALINK_MAX30102_WINDOW - 1U) * sizeof(float));

        memmove(sensor->ir_samples,
                sensor->ir_samples + 1U,
                (HEALINK_MAX30102_WINDOW - 1U) * sizeof(float));

        sensor->red_samples[HEALINK_MAX30102_WINDOW - 1U] = (float)red;
        sensor->ir_samples[HEALINK_MAX30102_WINDOW - 1U] = (float)ir;
    }

    return 0;
}

static void max30102_recompute(max30102_t *sensor)
{
    float optical_quality = 0.0f;
    float beat_quality = 0.0f;

    if (sensor->sample_count < MAX30102_WINDOW_REQUIRED) {
        sensor->signal_quality = 0.0f;
        sensor->hr_bpm = 0.0f;
        sensor->spo2_pct = 0.0f;
        sensor->valid = false;
        return;
    }

    sensor->ir_dc = mean(sensor->ir_samples, sensor->sample_count);
    sensor->red_dc = mean(sensor->red_samples, sensor->sample_count);
    sensor->ir_ac = rms_ac(sensor->ir_samples, sensor->sample_count, sensor->ir_dc);
    sensor->red_ac = rms_ac(sensor->red_samples, sensor->sample_count, sensor->red_dc);

    /*
     * --------------------------------------------------------
     * Finger-presence gate
     * --------------------------------------------------------
     *
     * Normalized AC/DC can be misleading when the sensor is
     * uncovered: a small amount of optical/background noise
     * divided by a very small DC level can look artificially
     * "pulsatile". Require an absolute optical DC level before
     * HR/SpO2 extraction is even attempted.
     *
     * Engineering bring-up values are intentionally far below
     * the measured finger-present DC levels (~100k/139k) but
     * well above the uncovered baseline (~0.6k/0.9k).
     */
    if (sensor->ir_dc < HEALINK_MAX30102_FINGER_IR_DC_MIN ||
        sensor->red_dc < HEALINK_MAX30102_FINGER_RED_DC_MIN) {
        sensor->signal_quality = 0.0f;
        sensor->hr_bpm = 0.0f;
        sensor->spo2_pct = 0.0f;
        sensor->valid = false;
        return;
    }

    if (sensor->ir_dc > 1.0f && sensor->red_dc > 1.0f && sensor->ir_ac > 0.0f) {
        const float pulsatility = sensor->ir_ac / sensor->ir_dc;
        const float red_pulsatility = sensor->red_ac / sensor->red_dc;

        /* Optical quality is deliberately conservative: near-flat and grossly
         * overdriven signals are not allowed to produce a valid vital sign. */
        if (pulsatility < 0.002f || pulsatility > 0.50f ||
            red_pulsatility < 0.0005f || red_pulsatility > 0.50f) {
            optical_quality = 0.0f;
        } else if (pulsatility < 0.01f) {
            optical_quality = pulsatility / 0.01f;
        } else if (pulsatility <= 0.20f) {
            optical_quality = 1.0f;
        } else {
            optical_quality = (0.50f - pulsatility) / 0.30f;
        }
    }

    if (optical_quality > 1.0f) optical_quality = 1.0f;
    if (optical_quality < 0.0f) optical_quality = 0.0f;

    sensor->hr_bpm = detect_hr(
        sensor->ir_samples,
        sensor->sample_count,
        MAX30102_SAMPLE_HZ,
        &beat_quality);

    if (sensor->ir_dc > 1.0f &&
        sensor->red_dc > 1.0f &&
        sensor->ir_ac > 1.0f &&
        sensor->red_ac > 0.0f) {

        const float ratio =
            (sensor->red_ac / sensor->red_dc) /
            (sensor->ir_ac / sensor->ir_dc);
        float spo2 = 110.0f - 25.0f * ratio;

        if (spo2 < 70.0f) spo2 = 70.0f;
        if (spo2 > 100.0f) spo2 = 100.0f;
        sensor->spo2_pct = spo2;
    } else {
        sensor->spo2_pct = 0.0f;
    }

    /* Combine optical quality and beat-to-beat consistency. A strong optical
     * signal with inconsistent peaks is intentionally not presented as a
     * high-confidence HR source. */
    sensor->signal_quality = optical_quality *
        (0.35f + 0.65f * beat_quality);

    if (sensor->hr_bpm < 30.0f || sensor->hr_bpm > 200.0f ||
        sensor->spo2_pct < HEALINK_MIN_SPO2_PCT ||
        sensor->spo2_pct > HEALINK_MAX_SPO2_PCT ||
        beat_quality < 0.30f ||
        optical_quality < 0.30f) {
        /*
         * Never leave a numerically calculated but invalid vital
         * sign visible to the fusion/UI layer.
         */
        sensor->hr_bpm = 0.0f;
        sensor->spo2_pct = 0.0f;
        sensor->valid = false;
    } else {
        sensor->valid = true;
    }
}

int max30102_init(max30102_t *sensor,
                  healink_i2c_t *i2c,
                  uint8_t address)
{
    uint8_t part_id;

    if (sensor == NULL || i2c == NULL || i2c->file_descriptor < 0) {
        errno = EINVAL;
        return -1;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->i2c_bus = i2c;
    sensor->i2c_address = address;

    if (max30102_read_reg(
            sensor,
            REG_PART_ID,
            &part_id) != 0) {
        return -1;
    }

    if (part_id != 0x15u) {
        errno = ENODEV;
        return -1;
    }

    if (max30102_write_reg(
            sensor,
            REG_MODE_CONFIG,
            0x40u) != 0) {
        return -1;
    }

    /* Datasheet reset completion is polled through MODE_CONFIG below. */
    {
        unsigned attempts;
        uint8_t mode;

        for (attempts = 0U; attempts < 20U; ++attempts) {
            if (max30102_read_reg(
                    sensor,
                    REG_MODE_CONFIG,
                    &mode) != 0) {
                return -1;
            }

            if ((mode & 0x40u) == 0U) {
                break;
            }

            {
                struct timespec delay = {0, 5000000L};
                if (nanosleep(&delay, NULL) != 0) {
                    return -1;
                }
            }
        }

        if (attempts == 20U) {
            errno = ETIMEDOUT;
            return -1;
        }
    }

    /* 1-sample averaging, rollover enabled, almost-full threshold 15. */
    if (max30102_write_reg(
            sensor,
            REG_FIFO_CONFIG,
            0x1Fu) != 0) {
        return -1;
    }

    if (max30102_clear_fifo(sensor) != 0) {
        return -1;
    }

    /* ADC range 4096 nA, 100 sps, 411 us / 18-bit pulse width. */
    if (max30102_write_reg(
            sensor,
            REG_SPO2_CONFIG,
            0x27u) != 0) {
        return -1;
    }

    if (max30102_write_reg(
            sensor,
            REG_LED1_PA,
            0x24u) != 0) {
        return -1;
    }

    if (max30102_write_reg(
            sensor,
            REG_LED2_PA,
            0x24u) != 0) {
        return -1;
    }

    if (max30102_write_reg(
            sensor,
            REG_INT_ENABLE1,
            0x00u) != 0) {
        return -1;
    }

    if (max30102_write_reg(
            sensor,
            REG_INT_ENABLE2,
            0x00u) != 0) {
        return -1;
    }

    if (max30102_write_reg(
            sensor,
            REG_MODE_CONFIG,
            0x03u) != 0) {
        return -1;
    }

    sensor->valid = false;
    sensor->sample_count = 0U;
    sensor->last_ts_ns = 0ULL;

    return 0;
}

int max30102_read_sample(max30102_t *sensor, uint64_t sample_time_ns)
{
    uint8_t write_ptr;
    uint8_t read_ptr;
    uint8_t overflow;
    unsigned available;
    unsigned i;

    if (sensor == NULL ||
        sensor->i2c_bus == NULL ||
        sensor->i2c_bus->file_descriptor < 0) {
        errno = EINVAL;
        return -1;
    }

    /*
     * Freshness is tied to actual FIFO contents, not merely to a successful
     * I2C transaction against FIFO_DATA.
     */
    if (max30102_read_reg(
            sensor,
            REG_FIFO_OVF,
            &overflow) != 0 ||
        max30102_read_reg(
            sensor,
            REG_FIFO_WR,
            &write_ptr) != 0 ||
        max30102_read_reg(
            sensor,
            REG_FIFO_RD,
            &read_ptr) != 0) {

        sensor->valid = false;
        return -1;
    }

    if (overflow != 0U) {
        /* A wrapped FIFO means samples were lost; reset the acquisition window. */
        sensor->sample_count = 0U;
        sensor->valid = false;

        if (max30102_clear_fifo(sensor) != 0) {
            return -1;
        }

        errno = EOVERFLOW;
        return -1;
    }

    available = max30102_fifo_available(
        write_ptr,
        read_ptr);

    if (available == 0U) {
        sensor->valid = false;
        errno = EAGAIN;
        return -1;
    }

    /*
     * A normal 100 Hz task sees one new sample per invocation.  Allow a
     * bounded backlog so transient scheduling delays can be recovered without
     * making an unbounded sensor-thread workload.  If the FIFO backlog is
     * larger than this bound, discard the window rather than reporting a
     * falsely fresh health value.
     */
    if (available > MAX30102_MAX_DRAIN_PER_CYCLE) {
        sensor->sample_count = 0U;
        sensor->valid = false;

        if (max30102_clear_fifo(sensor) != 0) {
            return -1;
        }

        errno = EOVERFLOW;
        return -1;
    }

    for (i = 0U; i < available; ++i) {
        uint8_t raw[6];
        uint32_t red;
        uint32_t ir;

        if (healink_i2c_read_regs(
                sensor->i2c_bus,
                sensor->i2c_address,
                REG_FIFO_DATA,
                raw,
                sizeof(raw)) != 0) {

            sensor->valid = false;
            return -1;
        }

        red = ((uint32_t)raw[0] << 16U) |
              ((uint32_t)raw[1] << 8U) |
              (uint32_t)raw[2];

        ir = ((uint32_t)raw[3] << 16U) |
             ((uint32_t)raw[4] << 8U) |
             (uint32_t)raw[5];

        red &= 0x3FFFFu;
        ir &= 0x3FFFFu;

        (void)max30102_append_sample(
            sensor,
            red,
            ir);
    }

    /*
     * The newest drained FIFO sample is considered current at observation
     * time.  Because every available sample was drained, stale buffered data
     * is no longer silently re-labelled as a new sample.
     */
    sensor->last_ts_ns = sample_time_ns;

    max30102_recompute(sensor);

    return 0;
}

void max30102_get(max30102_t *sensor,
                  float *hr_bpm,
                  float *spo2_pct,
                  float *signal_quality,
                  bool *valid,
                  uint64_t *ts_ns)
{
    if (sensor == NULL) {
        return;
    }

    if (hr_bpm != NULL) {
        *hr_bpm = sensor->hr_bpm;
    }

    if (spo2_pct != NULL) {
        *spo2_pct = sensor->spo2_pct;
    }

    if (signal_quality != NULL) {
        *signal_quality = sensor->signal_quality;
    }

    if (valid != NULL) {
        *valid = sensor->valid;
    }

    if (ts_ns != NULL) {
        *ts_ns = sensor->last_ts_ns;
    }
}
