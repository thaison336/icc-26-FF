#ifndef SOMNIGUARD_FSM_H
#define SOMNIGUARD_FSM_H

/**
 * @file somniguard_fsm.h
 * @brief Bộ Não Điều Hướng FSM 2 Tầng & Sub-FSM Can Thiệp (Tích hợp AI Model & RTOS Task)
 *
 * Module này quản lý toàn bộ chu trình sống của SomniGuard:
 * 1. Top-Level FSM: INACTIVE, OFF_FINGER_SUSPEND, ACTIVE_MODE, NORMAL_SLEEP, DEEP_ANALYSIS.
 * 2. Sub-FSM Can Thiệp (Hàm trợ lý gom gọn): IDLE, MILD_VIBRATE, STRONG_VIBRATE, BLE_ALARM, EVALUATE_RECOVERY.
 * 3. Tích hợp Mô hình AI Classifier phân loại triệu chứng ngưng thở khi ở trạng thái DEEP_ANALYSIS.
 * 4. Hỗ trợ chạy thành một Task RTOS độc lập.
 */

#include "global.h"
#include "somniguard_buffer.h"
#include "somniguard_dsp.h"
#include "somniguard_motion.h"
#include "sensor_hub/sensor_hub.h"
#ifdef __cplusplus
extern "C"
{
#endif

/* Hằng số cấu hình thời gian chuyển trạng thái FSM (ms) */
#define FSM_INACTIVE_TIMEOUT_MS (1800000UL) // 30 phút không đeo -> INACTIVE
#define FSM_SLEEP_ENTER_TIME_MS (180000UL)  // 3 phút không cựa tay -> NORMAL_SLEEP
#define FSM_WAKE_MOTION_TIME_MS (300000UL)  // 300s cựa tay liên tục -> ACTIVE_MODE
#define FSM_MILD_VIB_DURATION_MS (3000UL)   // 3 giây rung nhẹ
#define FSM_STRONG_VIB_DURATION_MS (5000UL) // 5 giây rung mạnh
#define FSM_EVALUATE_TIMEOUT_MS (10000UL)   // 10 giây đánh giá phục hồi sau can thiệp

/* Ngưỡng phát hiện bất thường trong NORMAL_SLEEP */
#define FSM_SPO2_CRITICAL_THRESHOLD 93.0f   // Ngưỡng 1: SpO2 < 93% -> DEEP_ANALYSIS ngay
#define FSM_ANOMALY_SUSTAIN_MS (10000UL)    // Ngưỡng 2: SpO2 drop >= 4% kéo dài 10s -> DEEP_ANALYSIS

    /**
     * @brief Cấu trúc quản lý toàn bộ trạng thái FSM 2 Tầng và các cờ điều khiển ngoại vi
     */
    typedef struct
    {
        somniguard_top_fsm_state_t top_state;      // Trạng thái Top-Level hiện tại
        somniguard_top_fsm_state_t prev_top_state; // Trạng thái Top-Level trước đó
        somniguard_sub_fsm_state_t sub_state;      // Trạng thái Sub-FSM can thiệp hiện tại
        somniguard_active_state_t active_state;
        somniguard_normal_state_t normal_state;

        // quan ly raw data
        SensorHub *hub;
        // ham tinh toan
        somniguard_dsp_t dsp_pro;
        somniguard_motion_t motion_pro;
        somniguard_buffer_t buffer_pro;

        // Kết quả dữ liệu
        somniguard_dsp_result_t dsp_res;       // Kết quả từ DSP Engine
        somniguard_motion_result_t motion_res; // Kết quả từ Motion Engine
        somniguard_tensor_frame_t tensor_res;  // Buffer Tensor 40s

        uint32_t top_state_entry_ms;   // Thời điểm bắt đầu vào Top State hiện tại (ms)
        uint32_t sub_state_entry_ms;   // Thời điểm bắt đầu vào Sub State hiện tại (ms)
        uint32_t last_motion_time_ms;  // Thời điểm phát hiện cựa tay gần nhất (ms)
        uint32_t quiet_duration_ms;    // Thời gian giữ yên không cựa tay (ms)
        uint32_t wake_motion_start_ms; // Thời điểm bắt đầu cựa tay liên tục để thức giấc (ms)

        /* Kết quả chẩn đoán gần nhất từ AI Model */
        somniguard_ai_event_t last_ai_event; // tạm thời chỉ lưu dự đoán AI gần nhất, sau này có thể ghi vào mảng buffer

        /* Các chỉ thị đầu ra điều khiển phần cứng (Hardware Agnostic Control Outputs) */
        uint8_t vibrate_level;       // Cấp độ rung: 0 = Tắt, 1 = Nhẹ, 2 = Mạnh
        bool buzzer_alarm;           // Bật còi báo động khẩn cấp
        bool ble_sos_flag;           // Bật phát tín hiệu BLE SOS cứu hộ
        uint16_t requested_imu_freq; // Tần số IMU yêu cầu (25Hz hoặc 50Hz)
        uint16_t requested_ppg_freq; // Tần số PPG yêu cầu (1Hz hoặc 50Hz)

        /* Cờ đánh dấu đã thực hiện entry action 1 lần (tránh gọi lặp mỗi vòng lặp 100ms) */
        bool off_finger_entry_done;      // Đã clear FIFO/reset DSP khi vào OFF_FINGER_SUSPEND
        bool active_init_done;           // Đã config sensor khi vào SUB_ACTIVE_INIT
        bool sleep_buffering_entry_done; // Đã config sensor khi vào SUB_SLEEP_BUFFERING

        /* Biến theo dõi bất thường trong SUB_SLEEP_MONITORING */
        float    spo2_baseline;      // SpO2 baseline khi bắt đầu MONITORING (để phát hiện drop tương đối)
        uint32_t anomaly_detect_ms;  // Thời điểm bắt đầu đếm bất thường bền vững (0 = chưa phát hiện)
        bool     anomaly_sustained;  // Cờ: đang trong giai đoạn đếm bất thường bền vững
    } somniguard_fsm_t;

    /**
     * @brief Khởi tạo hệ thống FSM
     * @param fsm Con trỏ tới struct somniguard_fsm_t
     * @param hub Con trỏ tới SensorHub
     */
    void somniguard_fsm_init(somniguard_fsm_t *fsm, SensorHub *hub);

    /**
     * @brief Chuyển đổi trạng thái Top-Level FSM và cập nhật timestamp
     */
    void somniguard_fsm_set_top_state(somniguard_fsm_t *fsm, somniguard_top_fsm_state_t new_state);

    /**
     * @brief Chuyển đổi trạng thái Sub-FSM Can thiệp và cập nhật timestamp
     */
    void somniguard_fsm_set_sub_state(somniguard_fsm_t *fsm, somniguard_sub_fsm_state_t new_sub_state);
    void somniguard_fsm_set_active_state(somniguard_fsm_t *fsm, somniguard_active_state_t new_sub_state);
    void somniguard_fsm_set_normal_state(somniguard_fsm_t *fsm, somniguard_normal_state_t new_sub_state);
    /**
     * @brief Chạy mô hình AI phân loại tình trạng ngưng thở từ ma trận Tensor 40s.
     * @param buffer Con trỏ tới bộ đệm Tensor 40s x 4 đặc trưng
     * @return somniguard_ai_event_t Kết quả chẩn đoán của AI (AI_EVENT_NORMAL, AI_EVENT_APNEA_MILD, ...)
     */
    somniguard_ai_event_t somniguard_ai_predict(const somniguard_buffer_t *buffer);

    /**
     * @brief Hàm RTOS Task độc lập thực thi Bộ Não FSM định kỳ (100ms)
     * @param pvParameters Con trỏ tham số truyền vào (somniguard_fsm_t*)
     */
    void somniguard_fsm_task(void *pvParameters);

    /**
        * @brief Ham RTOS Task độc lập thực thi FSM trong state DEEP ANALYSIS

    */
    void somniguard_deep_analysis_task(void *pvParameters);

    /**
     *@brief Ham RTOS Task độc lập thực thi FSM trong state ACTIVE MODE
     */
    void somniguard_active_mode_task(void *pvParameters);

    /**
     *@brief Ham RTOS Task độc lập thực thi FSM trong state NORMAL SLEEP
     */
    void somniguard_normal_sleep_task(void *pvParameters);

    /**
     * @brief Hàm thực thi tắt nguồn thiết bị (tắt ngoại vi, phát thông báo BLE/LED)
     */
    void somniguard_power_off(somniguard_fsm_t *fsm);
    /**
     * @brief Hàm thực thi hiện thị trạng thái hiện tại của thiết bị bằng led
     */
    void somniguard_led_display(uint8_t stateDevice);
    /**
     * @brief Hàm thực thi điều khiển motor rung haptic theo mức độ
     * @param nhận vào ampHaptic thể hiện cường độ rung
     */
    void somniguard_haptic_motor(uint8_t ampHaptic, uint32_t time);

    /**
     * @brief Hàm thực thi phát ra BLE trong khẩn cấp hoặc gửi thông tin
     */
    void somniguard_BLE_control();

    /**
     * @brief hàm thực thi điều khiển phần cứng
     */
    void somniguard_fsm_apply_actuators(somniguard_fsm_t *fsm);

    const char *somniguard_top_state_str(somniguard_top_fsm_state_t state);
    const char *somniguard_sub_state_str(somniguard_sub_fsm_state_t state);
    const char *somniguard_active_state_str(somniguard_active_state_t state);
    const char *somniguard_normal_state_str(somniguard_normal_state_t state);
    const char *somniguard_ai_event_str(somniguard_ai_event_t event);

#ifdef __cplusplus
}
#endif

#endif // SOMNIGUARD_FSM_H