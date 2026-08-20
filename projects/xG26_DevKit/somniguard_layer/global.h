#ifndef SOMNIGUARD_GLOBAL_H
#define SOMNIGUARD_GLOBAL_H

/**
 * @file global.h
 * @brief SomniGuard Core Layer - Móng & Cấu hình hệ thống (Hardware-Agnostic Core Foundation)
 *
 * Lớp này định nghĩa các cấu trúc dữ liệu thuần C, hằng số cấu hình hệ thống,
 * trạng thái FSM và kiểu trả về dùng chung cho toàn bộ thuật toán SomniGuard.
 * Đảm bảo độc lập 100% với phần cứng MCU (EFR32, ESP32,...) và RTOS.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* =========================================================================
 * 1. SYSTEM CONFIGURATION MACROS
 * ========================================================================= */
//========================IMU DEFINE=====================//
#define IMU_SAMPLING_RATE_ACTIVE_HZ 25  // Tần số lấy mẫu IMU ở trạng thái ACTIVE (25Hz)
#define IMU_SAMPLING_RATE_SLEEP_HZ 50   // Tần số lấy mẫu IMU ở trạng thái NORMAL_SLEEP (50Hz)
#define IMU_MAX_WINDOW_SIZE 128         // Dung lượng tối đa bộ đệm IMU ring buffer (128 mẫu)
#define PARAM_IMU_MOTION_THRESHOLD 0.1f // Ngưỡng độ lệch chuẩn gia tốc phát hiện cựa tay (0.25g)
#define AVARAGE_SAMPLING_IMU 1
#define ARTIFACT_FLAG 0

//========================MAX30102 DEFINE=================//
#define PPG_SAMPLING_RATE_SLEEP_HZ 50 // Tần số lấy mẫu mặc định PPG (50Hz)
#define PPG_SAMPLING_RATE_ACTIVE_HZ 1 // Tần số lấy mẫu mặc định PPG (1Hz) => chỉ để kiểm tra xem người dùng có còn đeo thiết bị ko
#define DSP_WINDOW_SIZE 128           // Số mẫu cho window tính DSP PPG (SpO2 & HR)
#define DSP_STRIDE 25                 // Số mẫu stride cho window tính DSP PPG (SpO2 & HR)

/* Tensor Cấu trúc Dữ liệu cho AI / Apnea Detection (Cửa sổ 40s @ 50Hz = 2000 mẫu x 4 Cột) */
#define FEATURE_RATE_1HZ 1       // Tần số các chỉ số SpO2, BPM, Motion (1Hz)
#define FEATURE_RATE_IR_AC_HZ 25 // Tần số sóng PPG IR AC Normalized (50Hz)
#define TENSOR_WINDOW_SEC 30     // Cửa sổ thời gian 40 giây
#define TENSOR_MAX_ROWS 60
#define TENSOR_COLS 28 // 28 Cột: [0: SpO2 (1Hz), 1: BPM (1Hz), 2 - 26: PPG IR AC Norm (50Hz), 27: Motion (1Hz)]

/* Ngưỡng tham chiếu sinh lý học */
#define SPO2_MIN 75.0f                  // SpO2 tối thiểu hợp lệ (%)
#define SPO2_MAX 100.0f                 // SpO2 tối đa hợp lệ (%)
#define BPM_MIN 40.0f                   // Nhịp tim tối thiểu (BPM)
#define BPM_MAX 200.0f                  // Nhịp tim tối đa (BPM)
#define APNEA_DROP_THRESHOLD 4.0f       // Ngưỡng giảm SpO2 (%) cảnh báo Apnea
#define AC_AMP_DROP_THRESHOLD_PCT 40.0f // % giảm biên độ PPG AC so với nền → kích phát Apnea heuristic
    /* Ngưỡng & Tham số Motor Rung Haptic (Nguồn: Cori 2018, van Maanen 2013, Benoist 2017) */
#define HAPTIC_FREQ_HZ 175           // Tần số rung tối ưu 150–200Hz (thụ thể áp lực Meissner/Pacinian)
#define HAPTIC_BURST_RATE_HZ 10      // Nhịp xung ngắt quãng 10Hz (tránh lờn/trơ thụ thể)
#define HAPTIC_LEVEL_MILD_PCT 20     // ~0.2g → Cấp 1 nhẹ (SUB_INTERVENT_MILD_VIBRATE), không thức giấc
#define HAPTIC_LEVEL_STRONG_PCT 80   // ~1.0g → Cấp 2 mạnh (SUB_INTERVENT_STRONG_VIBRATE), max an toàn N3
#define HAPTIC_LEVEL_MODERATE_PCT 50 // ~0.5g → Cấp 2 vừa (SUB_INTERVENT_MODERATE_VIBRATE), thức giấc nhẹ
/** @brief Convert % cường độ → giá trị PWM raw (0–255, PWM_TOP_VALUE = 255) */
#define HAPTIC_PCT_TO_PWM(pct) ((uint8_t)(((uint32_t)(pct) * 255U) / 100U))

/** @brief Giá trị PWM tính sẵn cho từng cấp can thiệp */
#define HAPTIC_PWM_MILD HAPTIC_PCT_TO_PWM(HAPTIC_LEVEL_MILD_PCT)         // = 51  (20% duty)
#define HAPTIC_PWM_STRONG HAPTIC_PCT_TO_PWM(HAPTIC_LEVEL_STRONG_PCT)     // = 204 (80% duty)
#define HAPTIC_PWM_MODERATE HAPTIC_PCT_TO_PWM(HAPTIC_LEVEL_MODERATE_PCT) // = 127 (50% duty)
/** @brief Thời gian rung cho từng cấp can thiệp (ms) */
#define HAPTIC_DURATION_MILD_MS 7000U     // 7 giây – kích thích nhẹ giai đoạn 1
#define HAPTIC_DURATION_STRONG_MS 7000U   // 7 giây – đánh thức mạnh giai đoạn 2
#define HAPTIC_DURATION_MODERATE_MS 5000U // 5 giây – rung trung bình giai đoạn 2

    /* Các tư thế nằm người dùng (Posture Enum) */
    typedef enum
    {
        POSTURE_SUPINE = 0, // Nằm ngửa
        POSTURE_PRONE = 1,  // Nằm sấp
        POSTURE_LEFT = 2,   // Nghiêng trái
        POSTURE_RIGHT = 3,  // Nghiêng phải
        POSTURE_UNKNOWN = 4 // Đang cử động / Chưa xác định
    } somniguard_posture_t;

    /* =========================================================================
     * 2. MÃ TRẢ VỀ VÀ TRẠNG THÁI FSM (ENUMERATIONS)
     * ========================================================================= */

    /**
     * @brief Mã trạng thái trả về của các hàm trong Core Layer
     */
    typedef enum
    {
        SOMNIGUARD_OK = 0,              // Thao tác thành công
        SOMNIGUARD_ERR_INVALID_PARAM,   // Tham số đầu vào không hợp lệ
        SOMNIGUARD_ERR_BUFFER_FULL,     // Bộ đệm đầy
        SOMNIGUARD_ERR_BUFFER_EMPTY,    // Bộ đệm rỗng
        SOMNIGUARD_ERR_NO_DATA,         // Chưa có dữ liệu
        SOMNIGUARD_ERR_SIGNAL_POOR,     // Tín hiệu PPG quá yếu hoặc nhiễu
        SOMNIGUARD_ERR_MOTION_ARTIFACT, // Nhiễu do cử động tay lớn
        SOMNIGUARD_ERR_UNKNOWN          // Lỗi không xác định
    } somniguard_status_t;

    /**
     * @brief Trạng thái FSM chính của hệ thống SomniGuard (System Brain FSM)
     */
    typedef enum
    {
        FSM_TOP_INACTIVE = 0,       // Tắt nguồn / Đế sạc
        FSM_TOP_OFF_FINGER_SUSPEND, // Tuột/nhấc ngón tay (Tắt bớt LED, tiết kiệm pin)
        FSM_TOP_ACTIVE_MODE,        // Đang đeo nhưng còn thức / cựa quậy
        FSM_TOP_NORMAL_SLEEP,       // Đang ngủ bình thường (Ghi Tensor 40s)
        FSM_TOP_DEEP_ANALYSIS       // Nghi ngờ ngưng thở -> Kích hoạt Sub-FSM Can thiệp
    } somniguard_top_fsm_state_t;

    typedef enum
    {
        // substate trong deep analysis mode
        SUB_INTERVENT_IDLE = 0,
        SUB_INTERVENT_MILD_VIBRATE,     // Rung nhẹ cấp 1 (Kích thích nhẹ)
        SUB_INTERVENT_MODERATE_VIBRATE, // Rung trung bình cấp 2 (Kích thích vừa)
        SUB_INTERVENT_STRONG_VIBRATE,   // Rung mạnh với cường độ tăng dần(Đánh thức khẩn cấp)
        SUB_INTERVENT_BLE_ALARM,        // Còi + BLE cho cứu hộ
        SUB_INTERVENT_EVALUATE_RECOVERY // Đánh giá chỉ số phục hồi sau can thiệp

    } somniguard_sub_fsm_state_t;

    typedef enum
    {
        // sub state trong active mode
        SUB_ACTIVE_INIT,
        SUB_ACTIVE_WAKEFUL,
        SUB_ACTIVE_PRE_SLEEP,
        SUB_ACTIVE_SPOT_CHECK

    } somniguard_active_state_t;

    typedef enum
    {
        // sub state trong active mode
        SUB_SLEEP_BUFFERING,
        SUB_SLEEP_MONITORING,
        // SUB_SLEEP_MICRO_MOVEMENT

    } somniguard_normal_state_t;

    /**
     * @brief Kết quả chẩn đoán phân loại tình trạng từ Mô hình AI (AI Model Diagnosis Event)
     */
    typedef enum
    {
        AI_EVENT_NORMAL = 0,    // Bình thường / Không có bệnh lý
        AI_EVENT_HYPOPNIA,      // Giảm thở (Hypopnia)
        AI_EVENT_APNEA_MILD,    // Ngưng thở mức độ nhẹ
        AI_EVENT_APNEA_SEVERE,  // Ngưng thở mức độ nặng (Sụt oxy sâu)
        AI_EVENT_APNEA_CRITICAL // Ngưng thở nguy hiểm cấp cứu (Báo động khẩn)
    } somniguard_ai_event_t;

    /* =========================================================================
     * 3. CẤU TRÚC DỮ LIỆU CẢM BIẾN RAW (HARDWARE AGNOSTIC DATA STRUCTURES)
     * ========================================================================= */

    /**
     * @brief Dữ liệu thô từ cảm biến quang học PPG (MAX30102)
     */
    typedef struct
    {
        uint32_t ir;  // Giá trị ADC kênh Hồng ngoại (IR)
        uint32_t red; // Giá trị ADC kênh Đỏ (Red)
    } somniguard_raw_ppg_t;

    /**
     * @brief Dữ liệu thô từ cảm biến gia tốc và góc quay IMU
     */
    typedef struct
    {
        float ax, ay, az; // Gia tốc 3 trục (g hoặc m/s^2)
        float gx, gy, gz; // Vận tốc góc 3 trục (dps)
    } somniguard_raw_imu_t;

    /**
     * @brief Gói dữ liệu thô tổng hợp truyền từ Tầng HAL xuống Core Layer
     */
    typedef struct
    {
        somniguard_raw_ppg_t ppg;
        somniguard_raw_imu_t imu;
        uint32_t timestamp_ms; // Thời gian lấy mẫu (ms)
    } somniguard_sensor_raw_t;

    /* =========================================================================
     * 4. CẤU TRÚC DỮ LIỆU KẾT QUẢ XỬ LÝ (PROCESSING RESULT STRUCTURES)
     * ========================================================================= */

    /**
     * @brief Kết quả tính toán từ Module DSP (PPG Signal Processing)
     */
    typedef struct
    {
        float spo2;        // Nồng độ Oxy trong máu (%)
        float heart_rate;  // Nhịp tim (BPM)
        float ac_red;      // Biên độ AC kênh Red
        float dc_red;      // Thành phần DC kênh Red
        float ac_ir;       // Biên độ AC kênh IR
        float dc_ir;       // Thành phần DC kênh IR
        float r_value;     // Tỉ lệ Ratio-of-Ratios (R)
        bool signal_valid; // Cờ báo tín hiệu đạt chất lượng tin cậy
    } somniguard_dsp_result_t;

    /**
     * @brief Kết quả phân tích nhiễu chuyển động từ Module Motion
     */
    typedef struct
    {
        float motion_energy; // Độ lớn năng lượng chuyển động (Magnitude/Variance/StdDev)
        bool is_moving;      // Cờ báo phát hiện cử động tay lớn (a_std > threshold)
        uint8_t posture;     // Tư thế người dùng (POSTURE_SUPINE, POSTURE_PRONE, POSTURE_LEFT, POSTURE_RIGHT)
    } somniguard_motion_result_t;

    /**
     * @brief Cấu trúc ma trận Tensor (tối đa 200x4 cho 40s @ 50Hz) phục vụ AI / Apnea Detection
     */
    typedef struct
    {
        float data[TENSOR_MAX_ROWS][TENSOR_COLS]; // Ma trận tối đa 200x4 (40s @ 5Hz) hoặc 40x4 (40s @ 1Hz)
        uint16_t count;                           // Số hàng thực tế đã lấp đầy trong bộ đệm
        bool is_full;                             // Cờ báo bộ đệm đã lấp đầy cửa sổ 40s
    } somniguard_tensor_frame_t;

/* =========================================================================
 * 5. MACRO TIỆN ÍCH HỖ TRỢ (UTILITY MACROS)
 * ========================================================================= */
#ifndef SOMNIGUARD_MIN
#define SOMNIGUARD_MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef SOMNIGUARD_MAX
#define SOMNIGUARD_MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef SOMNIGUARD_CLAMP
#define SOMNIGUARD_CLAMP(val, min, max) (SOMNIGUARD_MIN(SOMNIGUARD_MAX((val), (min)), (max)))
#endif

#ifdef __cplusplus
}
#endif

#endif // SOMNIGUARD_GLOBAL_H
