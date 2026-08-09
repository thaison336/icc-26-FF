#ifndef SOMNIGUARD_DSP_H
#define SOMNIGUARD_DSP_H

/**
 * @file somniguard_dsp.h
 * @brief Module Xử lý Tín hiệu DSP (SpO2 & Heart Rate BPM) - Hardware Agnostic
 * 
 * Module này độc lập hoàn toàn với phần cứng cảm biến và RTOS:
 * - Lọc IIR HPF (loại bỏ baseline drift) & LPF (khử nhiễu cao tần AC)
 * - Máy trạng thái FSM săn đáy sóng thích ứng (BPM Calculation)
 * - Bộ đệm tròn 128 mẫu & Tính năng lượng RMS thu được chỉ số SpO2
 * - Bộ lọc Hampel loại bỏ nhiễu đỉnh đột biến (Motion Artifact Filter)
 */

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BPM_FILTER_SIZE 5
#define BPM_DEFAULT     75
// Hằng số bộ lọc IIR sơ cấp
#define HPF_ALPHA 0.97f // Theo dõi DC (bám chậm, loại bỏ baseline drift)
#define LPF_BETA  0.50f // Khử nhiễu cao tần trên AC

// Hằng số phương trình bậc 2 (SpO2 = SPO2_A - SPO2_B*R - SPO2_C*R^2)
#define SPO2_A 100.00f
#define SPO2_B -2.50f
#define SPO2_C 18.75f

// Hằng số kiểm soát chất lượng tín hiệu & giới hạn tốc độ biến đổi
#define SPO2_SMOOTH   0.40f // Trọng số mẫu mới (0.4 * new + 0.6 * old)
#define SQI_MIN_PI    0.001f // Perfusion Index tối thiểu (0.1%)
#define SQI_MAX_PI    0.200f // Perfusion Index tối đa (20.0%)
#define MAX_SPO2_DROP 2.50f  // % tối đa SpO2 được giảm trong 1 update (0.5s)

//

/**
 * @brief Máy trạng thái FSM săn đáy sóng để tính nhịp tim
 */
typedef enum
{
    BPM_STATE_WAIT_FOR_DIP = 0,
    BPM_STATE_HUNTING_VALLEY
} somniguard_bpm_state_t;

/**
 * @brief Cấu trúc dữ liệu lưu trữ toàn bộ trạng thái của DSP Engine
 */
typedef struct
{
    /* Trạng thái bộ lọc IIR sơ cấp */
    float dc_track_red;
    float dc_track_ir;
    float lpf_red_prev;
    float lpf_ir_prev;
    bool is_finger_attached;

    /* Trạng thái FSM bắt đáy sóng (BPM) */
    somniguard_bpm_state_t bpm_state;
    float valley_min_val;
    uint32_t valley_min_time;
    uint32_t last_beat_time;
    uint32_t samples_since_last_beat;
    float local_ac_ir_min;
    float beat_threshold;
    int bpm_history[BPM_FILTER_SIZE];
    uint8_t bpm_index;
    int smoothed_bpm;

    /* Trạng thái bộ đệm tròn tính SpO2 */
    float history_sq_red[DSP_WINDOW_SIZE];
    float history_sq_ir[DSP_WINDOW_SIZE];
    float history_dc_red[DSP_WINDOW_SIZE];
    float history_dc_ir[DSP_WINDOW_SIZE];
    uint16_t circular_index;
    uint16_t sample_count;
    uint16_t stride_counter;
    bool is_buffer_full;

    /* Kết quả SpO2 & Hampel Filter */
    float final_r;
    float final_spo2;
    float buf_spo2[5];
    bool is_first_calc;
} somniguard_dsp_t;

/**
 * @brief Khởi tạo/Reset toàn bộ bộ đệm và biến trạng thái của DSP Engine.
 * @param dsp Con trỏ tới struct somniguard_dsp_t
 */
void somniguard_dsp_init(somniguard_dsp_t *dsp);

/**
 * @brief Reset nhanh trạng thái khi phát hiện tuột/nhấc ngón tay khỏi sensor.
 * @param dsp Con trỏ tới struct somniguard_dsp_t
 */
void somniguard_dsp_reset(somniguard_dsp_t *dsp);

/**
 * @brief Cập nhật FSM bắt đáy sóng tính BPM cho từng mẫu AC IR (chạy mỗi mẫu 50Hz).
 * @param dsp Con trỏ tới struct somniguard_dsp_t
 * @param acIR_filtered Giá trị AC IR đã qua lọc IIR
 * @param timestamp_ms Thời gian hiện tại (ms)
 */
void somniguard_dsp_update_bpm(somniguard_dsp_t *dsp, float acIR_filtered, uint32_t timestamp_ms);

/**
 * @brief Tính toán SpO2 và R từ bộ đệm tròn khi đủ mẫu (DSP_WINDOW_SIZE = 128).
 * @param dsp Con trỏ tới struct somniguard_dsp_t
 * @param out_spo2 Con trỏ nhận giá trị SpO2 tính toán (%)
 * @param out_r Con trỏ nhận giá trị tỷ số Ratio-of-Ratios (R)
 * @return true nếu tính toán thành công, false nếu chưa đủ đệm hoặc tín hiệu lỗi
 */
bool somniguard_dsp_calculate_spo2(somniguard_dsp_t *dsp, float *out_spo2, float *out_r);

/**
 * @brief Hàm xử lý toàn bộ Pipeline cho 1 mẫu PPG thô (Red & IR).
 * @param dsp Con trỏ tới struct somniguard_dsp_t
 * @param raw_red Mẫu thô Red từ MAX30102 ADC
 * @param raw_ir Mẫu thô IR từ MAX30102 ADC
 * @param timestamp_ms Thời điểm lấy mẫu (ms)
 * @param result Con trỏ nhận kết quả xử lý (somniguard_dsp_result_t)
 * @return true nếu có kết quả SpO2 mới tại chu kỳ Stride (0.5s), false nếu chỉ cập nhật từng mẫu
 */
bool somniguard_dsp_process_sample(somniguard_dsp_t *dsp, uint32_t raw_red, uint32_t raw_ir, uint32_t timestamp_ms, somniguard_dsp_result_t *result);

#ifdef __cplusplus
}
#endif

#endif // SOMNIGUARD_DSP_H
