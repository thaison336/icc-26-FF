#include "somniguard_buffer.h"
#include <string.h>

/**
 * @brief Khởi tạo toàn bộ bộ đệm somniguard_buffer_t về 0.
 * @param buf Con trỏ tới struct somniguard_buffer_t
 */
void somniguard_buffer_init(somniguard_buffer_t *buf)
{
    if (buf == NULL)
        return;

    memset(buf, 0, sizeof(somniguard_buffer_t));
    somniguard_buffer_reset(buf);
}

/**
 * @brief Reset bộ đệm (xóa toàn bộ mẫu hiện có, reset các chỉ số 1Hz tạm thời).
 * @param buf Con trỏ tới struct somniguard_buffer_t
 */
void somniguard_buffer_reset(somniguard_buffer_t *buf)
{
    if (buf == NULL)
        return;

    buf->head = 0;
    buf->count = 0;
    buf->is_full = false;
    buf->last_spo2 = 0.0f;
    buf->last_bpm = 0.0f;
    buf->last_motion = 0.0f;
    memset(buf->ring_tensor, 0, sizeof(buf->ring_tensor));
}

/**
 * @brief Cập nhật các chỉ số sinh lý 1Hz (SpO2, BPM, Motion Level) khi có kết quả mới (mỗi 1s/stride).
 * @param buf Con trỏ tới struct somniguard_buffer_t
 * @param spo2 Giá trị SpO2 (%)
 * @param heart_rate Giá trị nhịp tim (BPM)
 * @param motion_level Năng lượng cựa tay IMU (g)
 * @return somniguard_status_t SOMNIGUARD_OK nếu thành công
 */
somniguard_status_t somniguard_buffer_update_1hz_metrics(somniguard_buffer_t *buf,
                                                         float spo2,
                                                         float heart_rate,
                                                         float motion_level)
{
    if (buf == NULL)
        return SOMNIGUARD_ERR_INVALID_PARAM;

    buf->last_spo2 = spo2;
    buf->last_bpm = heart_rate;
    buf->last_motion = motion_level;

    return SOMNIGUARD_OK;
}

/**
 * @brief Nạp 1 mẫu sóng PPG IR AC Normalized tức thời (50Hz - mỗi 20ms).
 * Tự động kết hợp với các chỉ số 1Hz hiện tại bằng kỹ thuật Zero-Order Hold.
 * @param buf Con trỏ tới struct somniguard_buffer_t
 * @param ac_ir_norm Giá trị sóng PPG IR AC đã qua chuẩn hóa (AC_IR / DC_IR)
 * @return somniguard_status_t SOMNIGUARD_OK nếu thành công
 */
somniguard_status_t somniguard_buffer_push_50hz(somniguard_buffer_t *buf, float spo2, float bpm, float *ac_ir_norm, float motion_level)
{
    if (buf == NULL || ac_ir_norm == NULL)
        return SOMNIGUARD_ERR_INVALID_PARAM;

    buf->ring_tensor[buf->head][0] = spo2;
    buf->ring_tensor[buf->head][1] = bpm;
    for (int i = 0; i < FEATURE_RATE_IR_AC_HZ; i++)
    {
        if ((2 + i) < TENSOR_COLS) {
            buf->ring_tensor[buf->head][2 + i] = ac_ir_norm[i];
        }
    }
    buf->ring_tensor[buf->head][27] = motion_level;

    buf->head = (buf->head + 1) % TENSOR_MAX_ROWS;

    // Cập nhật số lượng mẫu và cờ đầy đệm
    if (buf->count < TENSOR_MAX_ROWS)
    {
        buf->count++;
    }

    if (buf->count >= TENSOR_MAX_ROWS)
    {
        buf->is_full = true;
    }

    return SOMNIGUARD_OK;
}

/**
 * @brief Nạp trực tiếp 1 hàng chứa đầy đủ 4 cột vào bộ đệm vòng.
 * @param buf Con trỏ tới struct somniguard_buffer_t
 * @param spo2 Giá trị SpO2 (%)
 * @param heart_rate Giá trị nhịp tim (BPM)
 * @param ac_ir_norm Giá trị PPG IR AC đã qua chuẩn hóa
 * @param motion_level Năng lượng cựa tay IMU (g)
 * @return somniguard_status_t SOMNIGUARD_OK nếu thành công
 */

/**
 * @brief Trích xuất ma trận Tensor (2000x4) được sắp xếp theo đúng trình tự thời gian
 * từ mẫu cũ nhất (Hàng 0) đến mẫu mới nhất (Hàng count-1).
 * @param buf Con trỏ tới struct somniguard_buffer_t
 * @param out_tensor Con trỏ tới struct somniguard_tensor_frame_t nhận ma trận sắp xếp
 * @return somniguard_status_t SOMNIGUARD_OK nếu trích xuất thành công
 */
somniguard_status_t somniguard_buffer_get_ordered_tensor(const somniguard_buffer_t *buf,
                                                         somniguard_tensor_frame_t *out_tensor)
{
    if (buf == NULL || out_tensor == NULL)
        return SOMNIGUARD_ERR_INVALID_PARAM;

    memset(out_tensor, 0, sizeof(somniguard_tensor_frame_t));
    out_tensor->count = buf->count;
    out_tensor->is_full = buf->is_full;

    if (buf->count == 0)
    {
        return SOMNIGUARD_OK;
    }

    // Nếu bộ đệm đã đầy, mẫu cũ nhất nằm ở vị trí head hiện tại.
    // Nếu bộ đệm chưa đầy, mẫu cũ nhất nằm ở vị trí 0.
    uint16_t start_index = buf->is_full ? buf->head : 0;

    for (uint16_t i = 0; i < buf->count; i++)
    {
        uint16_t ring_idx = (start_index + i) % TENSOR_MAX_ROWS;
        out_tensor->data[i][0] = buf->ring_tensor[ring_idx][0]; // SpO2
        out_tensor->data[i][1] = buf->ring_tensor[ring_idx][1]; // BPM
        for (int j = 0; j < FEATURE_RATE_IR_AC_HZ; j++)
        {
            if ((2 + j) < TENSOR_COLS) {
                out_tensor->data[i][2 + j] = buf->ring_tensor[ring_idx][2 + j]; // 25 cột PPG IR AC Norm
            }
        }
        out_tensor->data[i][27] = buf->ring_tensor[ring_idx][27]; // Motion Level
    }

    return SOMNIGUARD_OK;
}

/**
 * @brief Lấy mẫu đặc trưng sinh lý mới nhất vừa nạp vào đệm.
 * @param buf Con trỏ tới struct somniguard_buffer_t
 * @param out_spo2 Con trỏ nhận SpO2 mới nhất
 * @param out_hr Con trỏ nhận HR mới nhất
 * @param out_ac_ir_norm Con trỏ nhận PPG IR AC Norm mới nhất
 * @param out_motion Con trỏ nhận Motion Level mới nhất
 * @return somniguard_status_t SOMNIGUARD_OK nếu thành công, SOMNIGUARD_ERR_BUFFER_EMPTY nếu đệm rỗng
 */
somniguard_status_t somniguard_buffer_get_latest(const somniguard_buffer_t *buf,
                                                 float *out_spo2,
                                                 float *out_hr,
                                                 float *out_ac_ir_norm,
                                                 float *out_motion)
{
    if (buf == NULL)
        return SOMNIGUARD_ERR_INVALID_PARAM;

    if (buf->count == 0)
        return SOMNIGUARD_ERR_BUFFER_EMPTY;

    // Vị trí mẫu mới nhất vừa ghi nằm ngay trước head
    uint16_t latest_idx = (buf->head == 0) ? (TENSOR_MAX_ROWS - 1) : (buf->head - 1);

    if (out_spo2)
        *out_spo2 = buf->ring_tensor[latest_idx][0];
    if (out_hr)
        *out_hr = buf->ring_tensor[latest_idx][1];
    if (out_ac_ir_norm)
        *out_ac_ir_norm = buf->ring_tensor[latest_idx][2];
    if (out_motion)
        *out_motion = buf->ring_tensor[latest_idx][27];

    return SOMNIGUARD_OK;
}
