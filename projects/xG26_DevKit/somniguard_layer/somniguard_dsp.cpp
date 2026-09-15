#include "somniguard_dsp.h"

// HÃ m há»— trá»£ sáº¯p xáº¿p ná»•i bá»t ngáº¯n phá»¥c vá»¥ lá»c Trung vá»‹ Hampel
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
// HAMPEL FILTER â€” Loáº¡i bá» nhiá»…u Ä‘á»‰nh Ä‘á»™t biáº¿n (Motion Artifact)
static float apply_hampel_filter(somniguard_dsp_t *dsp, float newValue)
{
    // Äáº©y máº«u má»›i vÃ o bá»™ Ä‘á»‡m trÆ°á»£t 5 máº«u
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
    // NgÆ°á»¡ng lá»c nhiá»…u artifact Ä‘á»™t biáº¿n (ngÆ°á»¡ng tá»‘i thiá»ƒu 2.5% cho phÃ©p rá»›t SpO2 sinh lÃ½)
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
 * @brief Khá»Ÿi táº¡o/Reset toÃ n bá»™ bá»™ Ä‘á»‡m vÃ  biáº¿n tráº¡ng thÃ¡i cá»§a DSP Engine.
 * @param dsp Con trá» tá»›i struct somniguard_dsp_t
 */
void somniguard_dsp_init(somniguard_dsp_t *dsp)
{
    if (dsp == nullptr)
        return;
    memset(dsp, 0, sizeof(somniguard_dsp_t));
    somniguard_dsp_reset(dsp);
}

/**
 * @brief Reset nhanh tráº¡ng thÃ¡i khi phÃ¡t hiá»‡n tuá»™t/nháº¥c ngÃ³n tay khá»i sensor.
 * @param dsp Con trá» tá»›i struct somniguard_dsp_t
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
 * @brief Cáº­p nháº­t FSM báº¯t Ä‘Ã¡y sÃ³ng tÃ­nh BPM cho tá»«ng máº«u AC IR (cháº¡y má»—i máº«u 50Hz).
 * @param dsp Con trá» tá»›i struct somniguard_dsp_t
 * @param acIR_filtered GiÃ¡ trá»‹ AC IR Ä‘Ã£ qua lá»c IIR
 * @param timestamp_ms Thá»i gian hiá»‡n táº¡i (ms)
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
 * @brief TÃ­nh toÃ¡n SpO2 vÃ  R tá»« bá»™ Ä‘á»‡m trÃ²n khi Ä‘á»§ máº«u (DSP_WINDOW_SIZE = 128).
 * @param dsp Con trá» tá»›i struct somniguard_dsp_t
 * @param dsp Con trá»  tá»›i struct somniguard_dsp_t
 * @param out_spo2 Con trá»  nháº­n giÃ¡ trá»‹ SpO2 tÃ­nh toÃ¡n (%)
 * @param out_r Con trá»  nháº­n giÃ¡ trá»‹ tá»· sá»‘ Ratio-of-Ratios (R)
 * @return true náº¿u tÃ­nh toÃ¡n thÃ nh cÃ´ng, false náº¿u chÆ°a Ä‘á»§ Ä‘á»‡m hoáº·c tÃ­n hiá»‡u lá»—i
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
    dsp->rms_red = rmsRed;
    dsp->rms_ir = rmsIR;
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
        else
        {

            // 2. Làm mịn trượt (Exponential Moving Average)
            float spo2_new_ave = SPO2_SMOOTH * instant_SpO2 + (1.0f - SPO2_SMOOTH) * dsp->final_spo2;
            // 1. Lọc gai nhọn đột biến (Hampel Filter) trên mẫu tức thời trước
            float spo2_new = apply_hampel_filter(dsp, spo2_new_ave);
            // 3. Giới hạn tốc độ tụt SpO2 sinh lý
            if (spo2_new < dsp->final_spo2 - MAX_SPO2_DROP)
            {
                spo2_new = dsp->final_spo2 - MAX_SPO2_DROP;
            }
            dsp->final_spo2 = spo2_new;
            dsp->final_r = SPO2_SMOOTH * instant_R + (1.0f - SPO2_SMOOTH) * dsp->final_r;
        }

        if (out_spo2)
            *out_spo2 = dsp->final_spo2;
        if (out_r)
            *out_r = dsp->final_r;
        return sqi_ok;
    }

    return false;
}

/**
 * @brief HÃ m xá»­ lÃ½ toÃ n bá»™ Pipeline cho 1 máº«u PPG thÃ´ (Red & IR).
 * @param dsp Con trá»  tá»›i struct somniguard_dsp_t
 * @param raw_red Máº«u thÃ´ Red tá»« MAX30102 ADC
 * @param raw_ir Máº«u thÃ´ IR tá»« MAX30102 ADC
 * @param timestamp_ms Thá» i Ä‘iá»ƒm láº¥y máº«u (ms)
 * @param result Con trá»  nháº­n káº¿t quáº£ xá»­ lÃ½ (somniguard_dsp_result_t)
 * @return true náº¿u cÃ³ káº¿t quáº£ SpO2 má»›i táº¡i chu ká»³ Stride (0.5s), false náº¿u chá»‰ cáº­p nháº­t tá»«ng máº«u
 */
bool somniguard_dsp_process_sample(somniguard_dsp_t *dsp, uint32_t raw_red, uint32_t raw_ir, uint32_t timestamp_ms, somniguard_dsp_result_t *result)
{
    if (!dsp)
        return false;
    // 1. KIá»‚M TRA Há»ž SÃ NG HOáº¶C NHáº¤C NGÃ“N TAY
    //           if (raw_ir < 40000 || raw_red < 40000)
    // {
    //     somniguard_dsp_reset(dsp);
    //     if (result)
    //     {
    //         memset(result, 0, sizeof(somniguard_dsp_result_t));
    //         result->signal_valid = false;
    //     }
    //     return false;
    // }
    float red_f = (float)raw_red;
    float ir_f = (float)raw_ir;
    // 2. KHá»žI Táº O Ä Æ¯á»œNG Ná»€N KHI Vá»ªA Ä áº¶T TAY
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
    // 3. Lá»ŒC IIR SÆ  Cáº¤P (HPF + LPF)
    float acRed_raw = red_f - dsp->dc_track_red;
    dsp->dc_track_red = (1.0f - HPF_ALPHA) * red_f + HPF_ALPHA * dsp->dc_track_red;
    float acIR_raw = ir_f - dsp->dc_track_ir;
    dsp->dc_track_ir = (1.0f - HPF_ALPHA) * ir_f + HPF_ALPHA * dsp->dc_track_ir;
    float acRed_filtered = (1.0f - LPF_BETA) * acRed_raw + LPF_BETA * dsp->lpf_red_prev;
    dsp->lpf_red_prev = acRed_filtered;
    float acIR_filtered = (1.0f - LPF_BETA) * acIR_raw + LPF_BETA * dsp->lpf_ir_prev;
    dsp->lpf_ir_prev = acIR_filtered;
    // 4. BPM FSM: Cáº¬P NHáº¬T Tá»ªNG MáºªU
    somniguard_dsp_update_bpm(dsp, acIR_filtered, timestamp_ms);
    // 5. Náº P Máº¢NG VÃ’NG TRÃ’N
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
    // 6. CHá»ˆ TÃ NH SPO2 KHI Ä á»¦ DSP_STRIDE MáºªU (25 máº«u = 0.5s)
    if (dsp->is_buffer_full && dsp->stride_counter >= DSP_STRIDE)
    {
        dsp->stride_counter = 0;
        float current_spo2 = -1, current_r = -1;
        bool spo2_valid = somniguard_dsp_calculate_spo2(dsp, &current_spo2, &current_r);
        if (result)
        {
            result->spo2 = current_spo2;
            result->heart_rate = (float)dsp->smoothed_bpm;
            result->ac_red = dsp->rms_red;
            result->dc_red = dsp->dc_track_red;
            result->ac_ir = dsp->rms_ir;
            result->dc_ir = dsp->dc_track_ir;
            result->r_value = current_r;
            result->signal_valid = spo2_valid;
            // printf("here\r\n");
        }
        return true;
    }
    return false;
}
