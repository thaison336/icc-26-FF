#ifndef SOMNIGUARD_MOTION_H
#define SOMNIGUARD_MOTION_H

/**
 * @file somniguard_motion.h
 * @brief Module Gạt Nhiễu Cựa Tay IMU (Hardware-Agnostic & Ultra Low Power)
 *
 * Hỗ trợ cả 2 phương thức:
 * 1. Nạp từng 1 mẫu thời gian thực (Streaming 1 sample per call).
 * 2. Nạp mảng N mẫu IMU/giây (Batch processing N samples per call).
 */

#include "global.h"
#include "sensor_hub/sensor_hub.h"
#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Cấu trúc dữ liệu bộ đệm & quản lý trạng thái cho Module Motion
     */
    typedef struct
    {
        float ring_buffer[IMU_MAX_WINDOW_SIZE]; // Bộ đệm vòng lưu a_mag trong cửa sổ 1s
        uint16_t head;                          // Vị trí ghi mẫu tiếp theo
        uint16_t count;                         // Số lượng mẫu hiện có trong bộ đệm
        uint16_t window_size;                   // Kích thước cửa sổ trượt (25 cho 25Hz, 50 cho 50Hz)
        uint16_t sample_rate_hz;                // Tần số lấy mẫu hiện tại (25Hz hoặc 50Hz)
        float motion_threshold;                 // Ngưỡng phát hiện cựa tay (PARAM_IMU_MOTION_THRESHOLD = 0.018g / 18 mg)

        /* Biến tích lũy cho giải thuật O(1) siêu tiết kiệm năng lượng */
        float sum_a;    // Tổng trượt các mẫu a_mag
        float sum_sq_a; // Tổng trượt bình phương các mẫu a_mag

        somniguard_motion_result_t last_result; // Kết quả phân tích mới nhất
    } somniguard_motion_t;

    /**
     * @brief Khởi tạo module Motion với tần số lấy mẫu ban đầu
     * @param motion Con trỏ tới struct quản lý motion
     * @param sample_rate_hz Tần số lấy mẫu IMU ban đầu (25Hz hoặc 50Hz)
     */
    void somniguard_motion_init(somniguard_motion_t *motion, uint16_t sample_rate_hz);

    /**
     * @brief Cập nhật linh hoạt tần số lấy mẫu IMU khi FSM chuyển trạng thái (ACTIVE <-> NORMAL_SLEEP)
     * @param motion Con trỏ tới struct quản lý motion
     * @param sample_rate_hz Tần số mới (25Hz cho ACTIVE, 50Hz cho NORMAL_SLEEP)
     */
    void somniguard_motion_set_sample_rate(somniguard_motion_t *motion, uint16_t sample_rate_hz);

    /**
     * @brief Điều chỉnh ngưỡng độ lệch chuẩn phát hiện cử động tay (mặc định 0.25g)
     * @param motion Con trỏ tới struct quản lý motion
     * @param threshold Ngưỡng g (ví dụ 0.25f)
     */
    void somniguard_motion_set_threshold(somniguard_motion_t *motion, float threshold);

    /**
     * @brief Reset toàn bộ bộ đệm ring buffer và các biến tích lũy
     * @param motion Con trỏ tới struct quản lý motion
     */
    void somniguard_motion_reset(somniguard_motion_t *motion);

    /**
     * @brief [PHƯƠNG THỨC 1 - STREAMING] Nạp 1 mẫu IMU thô, tính toán a_mag, a_std và cập nhật cờ is_moving
     * @param motion Con trỏ tới struct quản lý motion
     * @param raw_imu Mẫu gia tốc 3 trục thô từ cảm biến IMU
     * @param result Con trỏ lưu kết quả xuất ra (nếu NULL sẽ cập nhật vào motion->last_result)
     * @return somniguard_status_t SOMNIGUARD_OK nếu xử lý thành công
     */
    somniguard_status_t somniguard_motion_process_sample(
        somniguard_motion_t *motion,
        const somniguard_raw_imu_t *raw_imu,
        somniguard_motion_result_t *result);

    /**
     * @brief [PHƯƠNG THỨC 2 - BATCH] Nhận mảng N mẫu IMU trong 1s, tính a_mag, a_std và xuất cờ is_moving
     * @param motion Con trỏ tới struct quản lý motion
     * @param raw_imu_array Mảng chứa N mẫu IMU thô
     * @param num_samples Số lượng mẫu N truyền vào (ví dụ 25 mẫu hoặc 50 mẫu)
     * @param result Con trỏ lưu kết quả đầu ra (a_std, is_moving, posture)
     * @return somniguard_status_t SOMNIGUARD_OK nếu xử lý thành công
     */
    somniguard_status_t somniguard_motion_process_buffer(
        somniguard_motion_t *motion,
        const somniguard_raw_imu_t *raw_imu_array,
        uint16_t num_samples,
        somniguard_motion_result_t *result);

    /**
     * @brief Ước lượng tư thế nằm (Posture) dựa trên thành phần vector gia tốc trọng lực
     * @param raw_imu Mẫu gia tốc 3 trục thô
     * @return uint8_t Mã tư thế (POSTURE_SUPINE, POSTURE_PRONE, POSTURE_LEFT, POSTURE_RIGHT)
     */
    uint8_t somniguard_motion_estimate_posture(const somniguard_raw_imu_t *raw_imu);

#ifdef __cplusplus
}
#endif

#endif // SOMNIGUARD_MOTION_H
