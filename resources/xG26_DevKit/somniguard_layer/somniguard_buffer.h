#ifndef SOMNIGUARD_BUFFER_H
#define SOMNIGUARD_BUFFER_H

/**
 * @file somniguard_buffer.h
 * @brief Module Quản lý Bộ đệm Tròn Đa Tần Số (Multi-rate Tensor Buffer: PPG AC 50Hz & Metrics 1Hz)
 *
 * Module này giải quyết bài toán Đa Tần Số (Multi-rate Sampling):
 * - Kênh sóng PPG IR AC Normalized được nạp liên tục ở tần số 50Hz (2000 mẫu trong 40s).
 * - Các chỉ số sinh lý (SpO2, BPM, Motion) được nạp ở tần số 1Hz.
 * - Áp dụng cơ chế Zero-Order Hold (Sample-and-Hold): Giữ nguyên giá trị 1Hz gần nhất cho 50 mẫu sóng 50Hz trong mỗi giây.
 * - Trích xuất ma trận Tensor 2000x4 chuẩn trình tự thời gian cho AI / Apnea Detection Model.
 */

#include "global.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Cấu trúc dữ liệu Ring Buffer cho ma trận Tensor 2000x4 (40s @ 50Hz)
     */
    typedef struct
    {
        float ring_tensor[TENSOR_MAX_ROWS][TENSOR_COLS]; // Ma trận bộ đệm vòng 2000x4
        uint16_t head;                                   // Vị trí ghi mẫu 50Hz tiếp theo (0 -> 1999)
        uint16_t count;                                  // Số lượng mẫu 50Hz hiện có trong bộ đệm (0 -> 2000)
        bool is_full;                                    // Cờ báo bộ đệm đã lấp đầy 2000 mẫu (40 giây)

        /* Bộ nhớ đệm tạm Zero-Order Hold cho các kênh 1Hz */
        float last_spo2;   // Giá trị SpO2 1Hz mới nhất
        float last_bpm;    // Giá trị BPM 1Hz mới nhất
        float last_motion; // Giá trị Motion 1Hz mới nhất
    } somniguard_buffer_t;

    /**
     * @brief Khởi tạo toàn bộ bộ đệm somniguard_buffer_t về 0.
     * @param buf Con trỏ tới struct somniguard_buffer_t
     */
    void somniguard_buffer_init(somniguard_buffer_t *buf);

    /**
     * @brief Reset bộ đệm (xóa toàn bộ mẫu hiện có, reset các chỉ số 1Hz tạm thời).
     * @param buf Con trỏ tới struct somniguard_buffer_t
     */
    void somniguard_buffer_reset(somniguard_buffer_t *buf);

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
                                                             float motion_level);

    /**
     * @brief Nạp 1 mẫu sóng PPG IR AC Normalized tức thời (50Hz - mỗi 20ms).
     * Tự động kết hợp với các chỉ số 1Hz hiện tại bằng kỹ thuật Zero-Order Hold.
     * @param buf Con trỏ tới struct somniguard_buffer_t
     * @param ac_ir_norm Giá trị sóng PPG IR AC đã qua chuẩn hóa (AC_IR / DC_IR)
     * @return somniguard_status_t SOMNIGUARD_OK nếu thành công
     */
    somniguard_status_t somniguard_buffer_push_50hz(somniguard_buffer_t *buf, float spo2, float bpm, float *ac_ir_norm, float motion_level);

    /**
     * @brief Nạp trực tiếp 1 hàng chứa đầy đủ 4 cột vào bộ đệm vòng.
     * @param buf Con trỏ tới struct somniguard_buffer_t
     * @param spo2 Giá trị SpO2 (%)
     * @param heart_rate Giá trị nhịp tim (BPM)
     * @param ac_ir_norm Giá trị PPG IR AC đã qua chuẩn hóa
     * @param motion_level Năng lượng cựa tay IMU (g)
     * @return somniguard_status_t SOMNIGUARD_OK nếu thành công
     */
    somniguard_status_t somniguard_buffer_push_full(somniguard_buffer_t *buf,
                                                    float spo2,
                                                    float heart_rate,
                                                    float ac_ir_norm,
                                                    float motion_level);

    /**
     * @brief Trích xuất ma trận Tensor (2000x4) được sắp xếp theo đúng trình tự thời gian
     * từ mẫu cũ nhất (Hàng 0) đến mẫu mới nhất (Hàng count-1).
     * @param buf Con trỏ tới struct somniguard_buffer_t
     * @param out_tensor Con trỏ tới struct somniguard_tensor_frame_t nhận ma trận sắp xếp
     * @return somniguard_status_t SOMNIGUARD_OK nếu trích xuất thành công
     */
    somniguard_status_t somniguard_buffer_get_ordered_tensor(const somniguard_buffer_t *buf,
                                                             somniguard_tensor_frame_t *out_tensor);

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
                                                     float *out_motion);

#ifdef __cplusplus
}
#endif

#endif // SOMNIGUARD_BUFFER_H
