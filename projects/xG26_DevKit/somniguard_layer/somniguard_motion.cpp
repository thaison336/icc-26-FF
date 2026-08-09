#include "somniguard_motion.h"

void somniguard_motion_init(somniguard_motion_t *motion, uint16_t sample_rate_hz)
{
    if (motion == NULL)
    {
        return;
    }

    memset(motion, 0, sizeof(somniguard_motion_t));
    motion->motion_threshold = PARAM_IMU_MOTION_THRESHOLD;
    somniguard_motion_set_sample_rate(motion, sample_rate_hz);
}

void somniguard_motion_set_sample_rate(somniguard_motion_t *motion, uint16_t sample_rate_hz)
{
    if (motion == NULL)
    {
        return;
    }

    if (sample_rate_hz == 0)
    {
        sample_rate_hz = IMU_SAMPLING_RATE_ACTIVE_HZ;
    }

    motion->sample_rate_hz = sample_rate_hz;

    uint16_t new_window_size = sample_rate_hz;
    if (new_window_size > IMU_MAX_WINDOW_SIZE)
    {
        new_window_size = IMU_MAX_WINDOW_SIZE;
    }

    if (motion->window_size != new_window_size)
    {
        motion->window_size = new_window_size;
        somniguard_motion_reset(motion);
    }
}

void somniguard_motion_set_threshold(somniguard_motion_t *motion, float threshold)
{
    if (motion != NULL && threshold > 0.0f)
    {
        motion->motion_threshold = threshold;
    }
}

void somniguard_motion_reset(somniguard_motion_t *motion)
{
    if (motion == NULL)
    {
        return;
    }

    memset(motion->ring_buffer, 0, sizeof(motion->ring_buffer));
    motion->head = 0;
    motion->count = 0;
    motion->sum_a = 0.0f;
    motion->sum_sq_a = 0.0f;
    memset(&motion->last_result, 0, sizeof(somniguard_motion_result_t));
    motion->last_result.posture = POSTURE_UNKNOWN;
}

uint8_t somniguard_motion_estimate_posture(const somniguard_raw_imu_t *raw_imu)
{
    if (raw_imu == NULL)
    {
        return POSTURE_UNKNOWN;
    }

    float ax = raw_imu->ax;
    float ay = raw_imu->ay;
    float az = raw_imu->az;

    if (fabsf(az) > fabsf(ax) && fabsf(az) > fabsf(ay))
    {
        if (az > 0.5f)
        {
            return POSTURE_SUPINE;
        }
        else if (az < -0.5f)
        {
            return POSTURE_PRONE;
        }
    }
    else if (fabsf(ay) > fabsf(ax))
    {
        if (ay > 0.5f)
        {
            return POSTURE_LEFT;
        }
        else if (ay < -0.5f)
        {
            return POSTURE_RIGHT;
        }
    }

    return POSTURE_SUPINE;
}

somniguard_status_t somniguard_motion_process_sample(
    somniguard_motion_t *motion,
    const somniguard_raw_imu_t *raw_imu,
    somniguard_motion_result_t *result)
{
    if (motion == NULL || raw_imu == NULL)
    {
        return SOMNIGUARD_ERR_INVALID_PARAM;
    }

    // 1. Tính độ lớn gia tốc: a_mag = sqrt(ax^2 + ay^2 + az^2)
    float ax = raw_imu->ax;
    float ay = raw_imu->ay;
    float az = raw_imu->az;
    float a_mag_sq = ax * ax + ay * ay + az * az;
    float a_mag = sqrtf(a_mag_sq);

    // 2. Cập nhật Running Sum O(1): trừ mẫu cũ bị đẩy ra khỏi cửa sổ trượt 1s
    if (motion->count >= motion->window_size)
    {
        float old_sample = motion->ring_buffer[motion->head];
        motion->sum_a -= old_sample;
        motion->sum_sq_a -= (old_sample * old_sample);
    }
    else
    {
        motion->count++;
    }

    // 3. Nạp mẫu mới vào Ring Buffer và cộng dồn Running Sum O(1)
    motion->ring_buffer[motion->head] = a_mag;
    motion->sum_a += a_mag;
    motion->sum_sq_a += a_mag_sq;

    motion->head = (motion->head + 1) % motion->window_size;

    // 4. Nếu chưa đủ 5 mẫu đầu tiên, khởi tạo kết quả mặc định
    if (motion->count < 5)
    {
        motion->last_result.motion_energy = 0.0f;
        motion->last_result.is_moving = false;
        motion->last_result.posture = somniguard_motion_estimate_posture(raw_imu);
        if (result != NULL)
        {
            *result = motion->last_result;
        }
        return SOMNIGUARD_OK;
    }

    // 5. TÍNH VARIANCE O(1): Var = E[X^2] - (E[X])^2
    float inv_n = 1.0f / (float)motion->count;
    float mean = motion->sum_a * inv_n;
    float variance = (motion->sum_sq_a * inv_n) - (mean * mean);
    if (variance < 0.0f)
    {
        variance = 0.0f;
    }

    // 6. TỐI ƯU NĂNG LƯỢNG: So sánh Variance với Threshold^2
    float threshold_sq = motion->motion_threshold * motion->motion_threshold;
    bool is_moving = (variance > threshold_sq);

    // 7. Cập nhật kết quả đầu ra
    float a_std = sqrtf(variance);
    motion->last_result.motion_energy = a_std;
    motion->last_result.is_moving = is_moving;
    motion->last_result.posture = somniguard_motion_estimate_posture(raw_imu);

    if (result != NULL)
    {
        *result = motion->last_result;
    }

    return SOMNIGUARD_OK;
}

somniguard_status_t somniguard_motion_process_buffer(
    somniguard_motion_t *motion,
    const somniguard_raw_imu_t *raw_imu_array,
    uint16_t num_samples,
    somniguard_motion_result_t *result)
{
    if (motion == NULL || raw_imu_array == NULL || num_samples == 0)
    {
        return SOMNIGUARD_ERR_INVALID_PARAM;
    }

    // // Cập nhật tần số mẫu nếu num_samples khác window_size hiện tại
    // if (motion->window_size != num_samples)
    // {
    //     somniguard_motion_set_sample_rate(motion, num_samples);
    // }

    // Nạp lần lượt N mẫu trong mảng vào giải thuật
    somniguard_status_t status = SOMNIGUARD_OK;
    for (uint16_t i = 0; i < num_samples; i++)
    {
        status = somniguard_motion_process_sample(motion, &raw_imu_array[i], result);
        if (status != SOMNIGUARD_OK)
        {
            break;
        }
    }

    return status;
}
