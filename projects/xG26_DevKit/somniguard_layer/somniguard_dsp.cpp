#include "somniguard_dsp.h"

// Hàm hỗ trợ sắp xếp nổi bọt ngắn phục vụ lọc Trung vị Hampel
static void sort_array_ascend(float *arr, int size)
{
    for (int i = 0; i < size - 1; i++)
    {
        for (int j = i + 1; j < size; j++)
        {
            if (arr[i] > arr[j])
            {
                float temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }
    }
}
// HAMPEL FILTER — Loại bỏ nhiễu đỉnh đột biến (Motion Artifact)
static float apply_hampel_filter(somniguard_dsp_t *dsp, float newValue)
{
    // Đẩy mẫu mới vào bộ đệm trượt 5 mẫu
    for (int i = 0; i < 4; ++i)
        dsp->buf_spo2[i] = dsp->buf_spo2[i + 1];
    dsp->buf_spo2[4] = newValue;
    float sorted_x[5];
    for (int i = 0; i < 5; ++i)
        sorted_x[i] = dsp->buf_spo2[i];
    sort_array_ascend(sorted_x, 5);
    float median_M = sorted_x[2];
    float dev[5];
    for (int i = 0; i < 5; ++i)
        dev[i] = fabsf(dsp->buf_spo2[i] - median_M);
    sort_array_ascend(dev, 5);
    float mad = dev[2];
    // Ngưỡng lọc nhiễu artifact đột biến (ngưỡng tối thiểu 2.5% cho phép rớt SpO2 sinh lý)
    float threshold = 3.0f * 1.4826f * mad;
    if (threshold < 2.5f)
        threshold = 2.5f;
    if (fabsf(newValue - median_M) > threshold)
    {
        return median_M;
    }
    return newValue;
}

/**
 * @brief Khởi tạo/Reset toàn bộ bộ đệm và biến trạng thái của DSP Engine.
 * @param dsp Con trỏ tới struct somniguard_dsp_t
 */
void somniguard_dsp_init(somniguard_dsp_t *dsp)
{
    if (dsp == nullptr)
        return;
    memset(dsp, 0, sizeof(somniguard_dsp_t));
    somniguard_dsp_reset(dsp);
}

/**
 * @brief Reset nhanh trạng thái khi phát hiện tuột/nhấc ngón tay khỏi sensor.
 * @param dsp Con trỏ tới struct somniguard_dsp_t
 */
void somniguard_dsp_reset(somniguard_dsp_t *dsp)
{
    if (dsp == nullptr)
        return;
    dsp->dc_track_red = 0.0f;
    dsp->dc_track_ir = 0.0f;
    dsp->lpf_red_prev = 0.0f;
    dsp->lpf_ir_prev = 0.0f;
    dsp->is_finger_attached = false;
    dsp->bpm_state = BPM_STATE_WAIT_FOR_DIP;
    dsp->valley_min_val = 0.0f;
    dsp->valley_min_time = 0;
    dsp->last_beat_time = 0;
    dsp->samples_since_last_beat = 0;
    dsp->local_ac_ir_min = 0.0f;
    dsp->beat_threshold = -150.0f;
    dsp->bpm_index = 0;
    dsp->smoothed_bpm = BPM_DEFAULT;
    for (int i = 0; i < BPM_FILTER_SIZE; i++)
        dsp->bpm_history[i] = BPM_DEFAULT;
    dsp->circular_index = 0;
    dsp->sample_count = 0;
    dsp->stride_counter = 0;
    dsp->is_buffer_full = false;
    memset(dsp->history_sq_red, 0, sizeof(dsp->history_sq_red));
    memset(dsp->history_sq_ir, 0, sizeof(dsp->history_sq_ir));
    memset(dsp->history_dc_red, 0, sizeof(dsp->history_dc_red));
    memset(dsp->history_dc_ir, 0, sizeof(dsp->history_dc_ir));
    dsp->final_r = 0.0f;
    dsp->final_spo2 = 98.0f;
    dsp->is_first_calc = true;
    for (int i = 0; i < 5; i++)
        dsp->buf_spo2[i] = 98.0f;
}

/**
 * @brief Cập nhật FSM bắt đáy sóng tính BPM cho từng mẫu AC IR (chạy mỗi mẫu 50Hz).
 * @param dsp Con trỏ tới struct somniguard_dsp_t
 * @param acIR_filtered Giá trị AC IR đã qua lọc IIR
 * @param timestamp_ms Thời gian hiện tại (ms)
 */
void somniguard_dsp_update_bpm(somniguard_dsp_t *dsp, float acIR_filtered, uint32_t timestamp_ms)
{
    if (!dsp)
        return;
    dsp->samples_since_last_beat++;
    if (acIR_filtered < dsp->local_ac_ir_min)
        dsp->local_ac_ir_min = acIR_filtered;
    switch (dsp->bpm_state)
    {
    case BPM_STATE_WAIT_FOR_DIP:
        if (acIR_filtered < dsp->beat_threshold && dsp->samples_since_last_beat > 15)
        {
            dsp->bpm_state = BPM_STATE_HUNTING_VALLEY;
            dsp->valley_min_val = acIR_filtered;
            dsp->valley_min_time = timestamp_ms;
        }
        break;
    case BPM_STATE_HUNTING_VALLEY:
        if (acIR_filtered < dsp->valley_min_val)
        {
            dsp->valley_min_val = acIR_filtered;
            dsp->valley_min_time = timestamp_ms;
        }
        if (acIR_filtered > (dsp->valley_min_val + 25.0f))
        {
            uint32_t delta_time = dsp->valley_min_time - dsp->last_beat_time;
            if (delta_time > 375 && delta_time < 1500)
            {
                float instant_bpm = 60000.0f / (float)delta_time;
                dsp->bpm_history[dsp->bpm_index] = (int)instant_bpm;
                dsp->bpm_index = (dsp->bpm_index + 1) % BPM_FILTER_SIZE;
                long bpmSum = 0;
                for (int i = 0; i < BPM_FILTER_SIZE; i++)
                    bpmSum += dsp->bpm_history[i];
                dsp->smoothed_bpm = bpmSum / BPM_FILTER_SIZE;
            }
            dsp->last_beat_time = dsp->valley_min_time;
            dsp->beat_threshold = dsp->valley_min_val * 0.60f;
            dsp->local_ac_ir_min = 0.0f;
            dsp->samples_since_last_beat = 0;
            dsp->bpm_state = BPM_STATE_WAIT_FOR_DIP;
        }
        break;
    }
    if (dsp->samples_since_last_beat > 100)
    {
        dsp->beat_threshold = dsp->local_ac_ir_min * 0.5f;
        if (dsp->beat_threshold > -20.0f)
            dsp->beat_threshold = -50.0f;
        dsp->local_ac_ir_min = 0.0f;
        dsp->samples_since_last_beat = 0;
        dsp->last_beat_time = timestamp_ms;
        dsp->bpm_state = BPM_STATE_WAIT_FOR_DIP;
    }
}

/**
 * @brief Tính toán SpO2 và R từ bộ đệm tròn khi đủ mẫu (DSP_WINDOW_SIZE = 128).
 * @param dsp Con trỏ tới struct somniguard_dsp_t
 * @param out_spo2 Con trỏ nhận giá trị SpO2 tính toán (%)
 * @param out_r Con trỏ nhận giá trị tỷ số Ratio-of-Ratios (R)
 * @return true nếu tính toán thành công, false nếu chưa đủ đệm hoặc tín hiệu lỗi
 */
bool somniguard_dsp_calculate_spo2(somniguard_dsp_t *dsp, float *out_spo2, float *out_r)
{
    if (dsp == nullptr || out_spo2 == nullptr || out_r == nullptr)
        return false;
    float sum_sq_red = 0.0f, sum_sq_ir = 0.0f;
    float sum_dc_red = 0.0f, sum_dc_ir = 0.0f;
    for (int i = 0; i < DSP_WINDOW_SIZE; i++)
    {
        sum_sq_red += dsp->history_sq_red[i];
        sum_sq_ir += dsp->history_sq_ir[i];
        sum_dc_red += dsp->history_dc_red[i];
        sum_dc_ir += dsp->history_dc_ir[i];
    }
    float rmsRed = sqrtf(sum_sq_red / (float)DSP_WINDOW_SIZE);
    float rmsIR = sqrtf(sum_sq_ir / (float)DSP_WINDOW_SIZE);
    float mean_dc_red = sum_dc_red / (float)DSP_WINDOW_SIZE;
    float mean_dc_ir = sum_dc_ir / (float)DSP_WINDOW_SIZE;
    if (rmsIR > 0.0f && mean_dc_red > 0.0f && mean_dc_ir > 0.0f)
    {
        float pi_ir = rmsIR / mean_dc_ir;
        float pi_red = rmsRed / mean_dc_red;
        bool sqi_ok = (pi_ir >= SQI_MIN_PI && pi_ir <= SQI_MAX_PI) &&
                      (pi_red >= SQI_MIN_PI && pi_red <= SQI_MAX_PI);
        float instant_R = (rmsRed / mean_dc_red) / (rmsIR / mean_dc_ir);
        float instant_SpO2 = SPO2_A - (SPO2_B * instant_R) - (SPO2_C * instant_R * instant_R);
        if (instant_SpO2 > 100.0f)
            instant_SpO2 = 100.0f;
        if (instant_SpO2 < 50.0f)
            instant_SpO2 = 50.0f;
        if (dsp->is_first_calc)
        {
            dsp->final_spo2 = instant_SpO2;
            dsp->final_r = instant_R;
            for (int hi = 0; hi < 5; hi++)
                dsp->buf_spo2[hi] = instant_SpO2;
            dsp->is_first_calc = false;
        }
        else if (sqi_ok)
        {
            float spo2_new = SPO2_SMOOTH * instant_SpO2 + (1.0f - SPO2_SMOOTH) * dsp->final_spo2;
            if (spo2_new < dsp->final_spo2 - MAX_SPO2_DROP)
            {
                spo2_new = dsp->final_spo2 - MAX_SPO2_DROP;
            }
            dsp->final_spo2 = spo2_new;
            dsp->final_r = SPO2_SMOOTH * instant_R + (1.0f - SPO2_SMOOTH) * dsp->final_r;
            dsp->final_spo2 = apply_hampel_filter(dsp, dsp->final_spo2);
        }
    }
    if (out_spo2)
        *out_spo2 = dsp->final_spo2;
    if (out_r)
        *out_r = dsp->final_r;
    return true;
}

/**
 * @brief Hàm xử lý toàn bộ Pipeline cho 1 mẫu PPG thô (Red & IR).
 * @param dsp Con trỏ tới struct somniguard_dsp_t
 * @param raw_red Mẫu thô Red từ MAX30102 ADC
 * @param raw_ir Mẫu thô IR từ MAX30102 ADC
 * @param timestamp_ms Thời điểm lấy mẫu (ms)
 * @param result Con trỏ nhận kết quả xử lý (somniguard_dsp_result_t)
 * @return true nếu có kết quả SpO2 mới tại chu kỳ Stride (0.5s), false nếu chỉ cập nhật từng mẫu
 */
bool somniguard_dsp_process_sample(somniguard_dsp_t *dsp, uint32_t raw_red, uint32_t raw_ir, uint32_t timestamp_ms, somniguard_dsp_result_t *result)
{
    if (!dsp)
        return false;
    // 1. KIỂM TRA HỞ SÁNG HOẶC NHẤC NGÓN TAY
    if (raw_ir < 40000 || raw_red < 40000)
    {
        somniguard_dsp_reset(dsp);
        if (result)
        {
            memset(result, 0, sizeof(somniguard_dsp_result_t));
            result->signal_valid = false;
        }
        return false;
    }
    float red_f = (float)raw_red;
    float ir_f = (float)raw_ir;
    // 2. KHỞI TẠO ĐƯỜNG NỀN KHI VỪA ĐẶT TAY
    if (!dsp->is_finger_attached)
    {
        dsp->dc_track_red = red_f;
        dsp->dc_track_ir = ir_f;
        dsp->lpf_red_prev = 0.0f;
        dsp->lpf_ir_prev = 0.0f;
        dsp->is_finger_attached = true;
        dsp->is_first_calc = true;
        dsp->last_beat_time = timestamp_ms;
        dsp->samples_since_last_beat = 0;
        return false;
    }
    // 3. LỌC IIR SƠ CẤP (HPF + LPF)
    float acRed_raw = red_f - dsp->dc_track_red;
    dsp->dc_track_red = (1.0f - HPF_ALPHA) * red_f + HPF_ALPHA * dsp->dc_track_red;
    float acIR_raw = ir_f - dsp->dc_track_ir;
    dsp->dc_track_ir = (1.0f - HPF_ALPHA) * ir_f + HPF_ALPHA * dsp->dc_track_ir;
    float acRed_filtered = (1.0f - LPF_BETA) * acRed_raw + LPF_BETA * dsp->lpf_red_prev;
    dsp->lpf_red_prev = acRed_filtered;
    float acIR_filtered = (1.0f - LPF_BETA) * acIR_raw + LPF_BETA * dsp->lpf_ir_prev;
    dsp->lpf_ir_prev = acIR_filtered;
    // 4. BPM FSM: CẬP NHẬT TỪNG MẪU
    somniguard_dsp_update_bpm(dsp, acIR_filtered, timestamp_ms);
    // 5. NẠP MẢNG VÒNG TRÒN
    uint16_t win_mask = DSP_WINDOW_SIZE - 1;
    dsp->history_sq_red[dsp->circular_index] = acRed_filtered * acRed_filtered;
    dsp->history_sq_ir[dsp->circular_index] = acIR_filtered * acIR_filtered;
    dsp->history_dc_red[dsp->circular_index] = dsp->dc_track_red;
    dsp->history_dc_ir[dsp->circular_index] = dsp->dc_track_ir;
    dsp->circular_index = (dsp->circular_index + 1) & win_mask;
    dsp->sample_count++;
    dsp->stride_counter++;
    if (!dsp->is_buffer_full && dsp->sample_count >= DSP_WINDOW_SIZE)
    {
        dsp->is_buffer_full = true;
    }
    // 6. CHỈ TÍNH SPO2 KHI ĐỦ DSP_STRIDE MẪU (25 mẫu = 0.5s)
    if (dsp->is_buffer_full && dsp->stride_counter >= DSP_STRIDE)
    {
        dsp->stride_counter = 0;
        float current_spo2 = -1, current_r = -1;
        if (somniguard_dsp_calculate_spo2(dsp, &current_spo2, &current_r))
        {
            if (result)
            {
                result->spo2 = current_spo2;
                result->heart_rate = (float)dsp->smoothed_bpm;
                result->ac_red = acRed_filtered;
                result->dc_red = dsp->dc_track_red;
                result->ac_ir = acIR_filtered;
                result->dc_ir = dsp->dc_track_ir;
                result->r_value = current_r;
                result->signal_valid = (current_spo2 >= SPO2_MIN && current_spo2 <= SPO2_MAX);
                // printf("here\r\n");
            }
            return true;
        }
    }
    return false;
}
