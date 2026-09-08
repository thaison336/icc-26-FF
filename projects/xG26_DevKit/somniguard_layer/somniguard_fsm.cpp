#include "somniguard_fsm.h"
#include "ble_notification_manager.h"
#include "model/model.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* String Helper Functions */
const char *somniguard_top_state_str(somniguard_top_fsm_state_t state)
{
    switch (state)
    {
    case FSM_TOP_INACTIVE:
        return "INACTIVE";
    case FSM_TOP_OFF_FINGER_SUSPEND:
        return "OFF_FINGER_SUSPEND";
    case FSM_TOP_ACTIVE_MODE:
        return "ACTIVE_MODE";
    case FSM_TOP_NORMAL_SLEEP:
        return "NORMAL_SLEEP";
    case FSM_TOP_DEEP_ANALYSIS:
        return "DEEP_ANALYSIS";
    default:
        return "UNKNOWN_TOP";
    }
}

const char *somniguard_sub_state_str(somniguard_sub_fsm_state_t state)
{
    switch (state)
    {
    case SUB_INTERVENT_IDLE:
        return "IDLE";
    case SUB_INTERVENT_MILD_VIBRATE:

        return "MILD_VIBRATE";
    case SUB_INTERVENT_MODERATE_VIBRATE:
        return "MODERATE_VIBRATE";
    case SUB_INTERVENT_STRONG_VIBRATE:
        return "STRONG_VIBRATE";
    case SUB_INTERVENT_BLE_ALARM:
        return "BLE_ALARM";
    case SUB_INTERVENT_EVALUATE_RECOVERY:
        return "EVALUATE_RECOVERY";
    default:
        return "UNKNOWN_INTERVENT";
    }
}

const char *somniguard_active_state_str(somniguard_active_state_t state)
{
    switch (state)
    {
    case SUB_ACTIVE_INIT:
        return "ACTIVE_INIT";
    case SUB_ACTIVE_WAKEFUL:
        return "ACTIVE_WAKEFUL";
    case SUB_ACTIVE_PRE_SLEEP:
        return "ACTIVE_PRE_SLEEP";
    case SUB_ACTIVE_SPOT_CHECK:
        return "ACTIVE_SPOT_CHECK";
    default:
        return "UNKNOWN_ACTIVE";
    }
}

const char *somniguard_normal_state_str(somniguard_normal_state_t state)
{
    switch (state)
    {
    case SUB_SLEEP_BUFFERING:
        return "SLEEP_BUFFERING";
    case SUB_SLEEP_MONITORING:
        return "SLEEP_MONITORING";
    default:
        return "UNKNOWN_SLEEP";
    }
}

const char *somniguard_ai_event_str(somniguard_ai_event_t event)
{
    switch (event)
    {
    case AI_EVENT_NORMAL:
        return "NORMAL";
    case AI_EVENT_HYPOPNIA:
        return "HYPOPNIA";
    case AI_EVENT_APNEA_MILD:
        return "APNEA_MILD";
    case AI_EVENT_APNEA_SEVERE:
        return "APNEA_SEVERE";
    case AI_EVENT_APNEA_CRITICAL:
        return "APNEA_CRITICAL";
    default:
        return "UNKNOWN_AI_EVENT";
    }
}

/* Hardware Actuator Weak Stubs */
__attribute__((weak)) void somniguard_led_display(uint8_t stateDevice)
{
    (void)stateDevice;
}

__attribute__((weak)) void somniguard_led_boot_sequence()
{
}

__attribute__((weak)) void somniguard_led_boot_success()
{
}

__attribute__((weak)) void somniguard_led_sleep_buffering_start()
{
}

__attribute__((weak)) void somniguard_haptic_motor(uint8_t ampHaptic, uint32_t time)
{
    (void)ampHaptic;
    (void)time;
}

__attribute__((weak)) void somniguard_BLE_control()
{
}

void somniguard_fsm_init(somniguard_fsm_t *fsm, SensorHub *hub)
{
    if (fsm == nullptr)
    {
        return;
    }

    // Reset toàn bộ struct fsm về 0
    memset(fsm, 0, sizeof(somniguard_fsm_t));
    fsm->hub = hub;
    // Khởi tạo các trạng thái FSM ban đầu
    fsm->top_state = FSM_TOP_ACTIVE_MODE;
    fsm->prev_top_state = FSM_TOP_INACTIVE;
    fsm->sub_state = SUB_INTERVENT_IDLE;
    fsm->active_state = SUB_ACTIVE_INIT;
    fsm->normal_state = SUB_SLEEP_BUFFERING;

    uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    fsm->top_state_entry_ms = now_ms;
    fsm->sub_state_entry_ms = now_ms;
    fsm->last_motion_time_ms = now_ms;

    // Khởi tạo các bộ xử lý dữ liệu con (DSP, Motion & Buffer)
    somniguard_dsp_init(&fsm->dsp_pro);
    somniguard_buffer_init(&fsm->buffer_pro);
    somniguard_motion_init(&fsm->motion_pro, IMU_SAMPLING_RATE_ACTIVE_HZ);

    // Khởi tạo các cờ kết quả và cờ điều khiển ngoại vi mặc định
    fsm->last_ai_event = AI_EVENT_NORMAL;
    fsm->vibrate_level = 0;
    fsm->buzzer_alarm = false;
    fsm->ble_sos_flag = false;
    fsm->requested_imu_freq = IMU_SAMPLING_RATE_ACTIVE_HZ;
    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_ACTIVE_HZ;

    // AGC đã xác nhận ngón tay trước khi FSM init → set is_finger_attached = true
    // Nếu để false (default của memset), FSM sẽ nhảy ngay vào OFF_FINGER_SUSPEND
    // và shutdown MAX30102 → deadlock toàn hệ thống
    fsm->dsp_pro.is_finger_attached = true;

    // Khởi tạo các cờ entry action (memset đã set = false)
    fsm->off_finger_entry_done = false;
    fsm->active_init_done = false;
    fsm->sleep_buffering_entry_done = false;
    fsm->inactive_entry_done = false;
    // printf("\r\n[FSM INIT] SomniGuard FSM initialized. Start TopState: %s, ActiveSubState: %s\r\n",
    //        somniguard_top_state_str(fsm->top_state),
    //        somniguard_active_state_str(fsm->active_state));
    // fflush(stdout);
}

void somniguard_fsm_set_top_state(somniguard_fsm_t *fsm, somniguard_top_fsm_state_t new_state)
{
    if (fsm == nullptr || fsm->top_state == new_state)
    {
        return;
    }

    uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    // printf("\r\n>>> [FSM TOP STATE TRANSITION] %s -> %s (at %lu ms) <<<\r\n",
    //        somniguard_top_state_str(fsm->top_state),
    //        somniguard_top_state_str(new_state),
    //        (unsigned long)now_ms);
    fflush(stdout);
    fsm->prev_top_state = fsm->top_state;
    fsm->top_state = new_state;
    fsm->top_state_entry_ms = now_ms;

    // // Phát gói tin BLE thông báo chuyển trạng thái hệ thống
    // somniguard_ble_notify_event(
    //     SOMNIGUARD_BLE_EVT_TYPE_POWER_SYSTEM,
    //     SOMNIGUARD_BLE_EVT_CODE_FSM_STATE_CHG,
    //     (uint16_t)fsm->prev_top_state,
    //     (uint16_t)new_state);
}

void somniguard_fsm_set_sub_state(somniguard_fsm_t *fsm, somniguard_sub_fsm_state_t new_sub_state)
{
    if (fsm == nullptr || fsm->sub_state == new_sub_state)
    {
        return;
    }

    uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    // printf("--> [FSM SUB-INTERVENT TRANSITION] %s -> %s (at %lu ms)\r\n",
    //        somniguard_sub_state_str(fsm->sub_state),
    //        somniguard_sub_state_str(new_sub_state),
    //        (unsigned long)now_ms);
    fflush(stdout);
    fsm->sub_state = new_sub_state;
    fsm->sub_state_entry_ms = now_ms;
}

void somniguard_fsm_set_active_state(somniguard_fsm_t *fsm, somniguard_active_state_t new_sub_state)
{
    if (fsm == nullptr || fsm->active_state == new_sub_state)
    {
        return;
    }

    uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    // printf("--> [FSM ACTIVE SUB TRANSITION] %s -> %s (at %lu ms)\r\n",
    //        somniguard_active_state_str(fsm->active_state),
    //        somniguard_active_state_str(new_sub_state),
    //        (unsigned long)now_ms);
    fflush(stdout);
    fsm->active_state = new_sub_state;
    fsm->sub_state_entry_ms = now_ms;
}

void somniguard_fsm_set_normal_state(somniguard_fsm_t *fsm, somniguard_normal_state_t new_sub_state)
{
    if (fsm == nullptr || fsm->normal_state == new_sub_state)
    {
        return;
    }

    uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    // printf("--> [FSM SLEEP SUB TRANSITION] %s -> %s (at %lu ms)\r\n",
    //        somniguard_normal_state_str(fsm->normal_state),
    //        somniguard_normal_state_str(new_sub_state),
    //        (unsigned long)now_ms);
    fflush(stdout);
    fsm->normal_state = new_sub_state;
    fsm->sub_state_entry_ms = now_ms;

    // Reset entry flag khi quay lại BUFFERING để entry-action chạy lại
    if (new_sub_state == SUB_SLEEP_BUFFERING)
    {
        fsm->sleep_buffering_entry_done = false;
        fsm->anomaly_detect_ms = 0;
        fsm->anomaly_sustained = false;
    }
}
void somniguard_power_off(somniguard_fsm_t *fsm)
{
    if (fsm == nullptr)
    {
        return;
    }

    // Tắt các ngoại vi chấp hành
    fsm->vibrate_level = 0;
    fsm->buzzer_alarm = false;
    fsm->ble_sos_flag = false;
    fsm->requested_imu_freq = 0;
    fsm->requested_ppg_freq = 0;

    // 2. Tắt dòng LED và Shutdown MAX30102
    if (fsm->hub != nullptr)
    {
        fsm->hub->MAX30102_driver().setPulseAmplitudeRed(0);
        fsm->hub->MAX30102_driver().setPulseAmplitudeIR(0);
        fsm->hub->MAX30102_driver().driver().shutDown(); // <-- Gửi lệnh Shutdown I2C
    }

    // Reset bộ đệm & thuật toán
    somniguard_dsp_reset(&fsm->dsp_pro);
    somniguard_buffer_reset(&fsm->buffer_pro);
    somniguard_motion_reset(&fsm->motion_pro);
    somniguard_fsm_apply_actuators(fsm);
}

somniguard_ai_event_t somniguard_ai_predict(const somniguard_buffer_t *buffer)
{
    if (buffer == NULL || buffer->count == 0)
    {
        return AI_EVENT_NORMAL;
    }

    static somniguard_tensor_frame_t frame;
    somniguard_buffer_get_ordered_tensor(buffer, &frame);

    if (frame.count < TENSOR_MAX_ROWS)
    {
        return AI_EVENT_NORMAL;
    }

    // Truyền ma trận Tensor 60x28 trực tiếp tới Mô hình AI
    int8_t pred = predict_window_confidence((const float *)frame.data);

    // Trường hợp mô hình AI chưa khởi tạo hoặc lỗi, dùng quy tắc an toàn dựa vào SpO2 dự phòng
    if (pred == -1)
    {
        float sum_spo2 = 0.0f;
        float min_spo2 = 100.0f;
        float max_spo2 = 0.0f;
        uint16_t valid_cnt = 0;

        for (uint16_t i = 0; i < frame.count; i++)
        {
            float s = frame.data[i][0];
            if (s >= 50.0f && s <= 100.0f)
            {
                sum_spo2 += s;
                if (s < min_spo2)
                    min_spo2 = s;
                if (s > max_spo2)
                    max_spo2 = s;
                valid_cnt++;
            }
        }

        if (valid_cnt == 0)
        {
            return AI_EVENT_NORMAL;
        }

        float mean_spo2 = sum_spo2 / (float)valid_cnt;
        float spo2_drop = max_spo2 - min_spo2;

        if (min_spo2 < 80.0f || spo2_drop >= 8.0f)
            return AI_EVENT_APNEA_CRITICAL;
        if (min_spo2 < 88.0f || spo2_drop >= 5.0f)
            return AI_EVENT_APNEA_SEVERE;
        if (min_spo2 < 92.0f || spo2_drop >= 3.0f)
            return AI_EVENT_APNEA_MILD;
        if (mean_spo2 < 94.0f)
            return AI_EVENT_HYPOPNIA;
        return AI_EVENT_NORMAL;
    }

    // Phân loại kết quả đầu ra mô hình AI thành các cấp độ sự kiện sinh lý
    somniguard_ai_event_t event = AI_EVENT_NORMAL;

    if (pred == 2)
    {
        event = AI_EVENT_APNEA_CRITICAL;
    }
    else if (pred == 1)
    {
        event = AI_EVENT_HYPOPNIA;
    }
    else if (pred == 0)
    {
        event = AI_EVENT_NORMAL;
    }

    printf("[AI DIAGNOSIS] Model outputEvent: %s\r\n", somniguard_ai_event_str(event));
    fflush(stdout);

    return event;
}

/**
 * @brief FREERTOS TASK WRAPPER CHO BỘ NÃO FSM
 * Thực thi Bộ Não FSM độc lập định kỳ (100ms / 10Hz)
 * @param pvParameters Con trỏ tới struct somniguard_fsm_t
 */
void somniguard_fsm_task(void *pvParameters)
{
    somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
    if (fsm == nullptr)
    {
        vTaskDelete(nullptr);
        return;
    }
    printf("--- FSM Main Task Started ---\r\n");

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // Thực thi 100ms một lần (10Hz)
    printf("hehehe");
    while (1)
    {
        // Chờ chính xác 100ms để chạy vòng lặp FSM định kỳ
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        uint32_t timestamp_ms = pdTICKS_TO_MS(xTaskGetTickCount());

        // =========================================================================
        // CẬP NHẬT CÁC BỘ ĐẾM THỜI GIAN CỬ ĐỘNG / NẰM YÊN (MOTIONS & QUIET TIMERS)
        // =========================================================================
        if (fsm->motion_res.is_moving)
        {
            // 1. Có cựa quậy: Reset thời gian nằm yên về 0
            fsm->last_motion_time_ms = timestamp_ms;
            fsm->quiet_duration_ms = 0;
            // 2. Ghi nhận thời điểm bắt đầu cựa quậy để đếm chu kỳ thức giấc (15s cựa quậy liên tục)
            if (fsm->wake_motion_start_ms == 0)
            {
                fsm->wake_motion_start_ms = timestamp_ms;
            }
        }
        else
        {

            fsm->quiet_duration_ms = timestamp_ms - fsm->last_motion_time_ms;
            fsm->wake_motion_start_ms = 0;
        }

        // =========================================================================
        // TOP-LEVEL FSM TRANSITIONS (Bảng 1)
        // =========================================================================
        switch (fsm->top_state)
        {
        case FSM_TOP_INACTIVE:
        {
            // ==========================================================
            // ENTRY ACTION: CHỈ CHẠY 1 LẦN DUY NHẤT KHI VỪA VÀO INACTIVE
            // ==========================================================
            if (!fsm->inactive_entry_done)
            {
                // 1. Tắt cơ cấu chấp hành (Rung, Còi)
                fsm->vibrate_level = 0;
                fsm->buzzer_alarm = false;
                fsm->ble_sos_flag = false;
                fsm->requested_imu_freq = 0;
                fsm->requested_ppg_freq = 0;
                somniguard_fsm_apply_actuators(fsm);
                // 2. Nhấp nháy LED báo hiệu tắt máy trong 5 giây (ví dụ chớp 5 lần, mỗi lần 1s)
                for (int i = 0; i < 5; i++)
                {
                    somniguard_led_display(FSM_TOP_INACTIVE); // Bật/Đổi trạng thái LED
                    vTaskDelay(pdMS_TO_TICKS(500));
                    somniguard_led_display(FSM_TOP_NORMAL_SLEEP); // Tắt LED
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                // Đảm bảo kết thúc 5s là TẮT HẲN LED
                somniguard_led_display(FSM_TOP_NORMAL_SLEEP);
                // 3. Tắt nguồn các cảm biến (MAX30102 & IMU) 1 lần duy nhất
                if (fsm->hub != nullptr)
                {
                    fsm->hub->MAX30102_driver().setPulseAmplitudeRed(0);
                    fsm->hub->MAX30102_driver().setPulseAmplitudeIR(0);
                    fsm->hub->MAX30102_driver().driver().shutDown();
                    fsm->hub->imu_driver().sleep(); // Đặt MPU6050 vào sleep mode (PWR_MGMT_1 SLEEP bit)
                }
                // 4. Gửi bản tin BLE báo đã chuyển sang INACTIVE 1 lần duy nhất
                somniguard_ble_notify_event(
                    SOMNIGUARD_BLE_EVT_TYPE_POWER_SYSTEM,
                    SOMNIGUARD_BLE_EVT_CODE_FSM_STATE_CHG,
                    (uint16_t)fsm->prev_top_state,
                    (uint16_t)fsm->top_state);
                fsm->inactive_entry_done = true; // Đánh dấu đã hoàn thành
            }
            // Sau khi đã xong Entry Action, task chỉ delay nhẹ chờ sự kiện bật nguồn
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }

        case FSM_TOP_OFF_FINGER_SUSPEND:
        {
            // Trạng thái tạm dừng do hở ngón tay
            // 1. Clear FIFO / Reset DSP CHỈ 1 LẦN khi entry để tránh nạp dữ liệu rác
            // 2. Nhắc nhở bằng LED & Rung nhẹ nếu trước đó ở ACTIVE

            somniguard_led_display(FSM_TOP_OFF_FINGER_SUSPEND);

            // Entry action: Chỉ thực hiện 1 lần khi mới vào state
            if (!fsm->off_finger_entry_done)
            {
                somniguard_ble_notify_event(
                    SOMNIGUARD_BLE_EVT_TYPE_SENSOR_STATUS,
                    SOMNIGUARD_BLE_EVT_CODE_FINGER_REMOVED,
                    (uint16_t)(timestamp_ms - fsm->top_state_entry_ms),
                    0);
                if (fsm->hub != nullptr)
                {
                    fsm->hub->MAX30102_driver().setPulseAmplitudeRed(0);
                    fsm->hub->MAX30102_driver().setPulseAmplitudeIR(0);
                    fsm->hub->MAX30102_driver().driver().shutDown();
                    fsm->hub->imu_driver().sleep();
                }
                fsm->requested_ppg_freq = 0;
                fsm->requested_imu_freq = 0;
                fsm->off_finger_entry_done = true;
            }

            // 2. CƠ CHẾ ĐỌC THỬ ĐỊNH KỲ (PROBE) MỖI 1000MS
            static uint32_t last_probe_ms = 0;
            if (timestamp_ms - last_probe_ms >= 1000UL)
            {
                last_probe_ms = timestamp_ms;
                if (fsm->hub != nullptr)
                {
                    // Bật MAX30102 trong 50ms để DataProcessingTask đọc thử mẫu mới thực tế từ phần cứng
                    fsm->hub->MAX30102_driver().driver().wakeUp();
                    fsm->hub->MAX30102_driver().setPulseAmplitudeIR(0x1F);
                    fsm->hub->MAX30102_driver().setPulseAmplitudeRed(0x1F);
                    fsm->hub->MAX30102_driver().clearFIFO();
                    fsm->hub->MAX30102_driver().setSampleRate(50);

                    vTaskDelay(pdMS_TO_TICKS(100));

                    uint8_t avail = fsm->hub->MAX30102_driver().available();
                    if (avail == 0)
                    {
                        continue;
                    }

                    // Đọc mẫu cuối cùng trong buffer (đã được interrupt task fill)
                    uint32_t ppgIR = 0;
                    // Xả hết trừ 1 mẫu cuối để lấy giá trị mới nhất
                    while (fsm->hub->MAX30102_driver().available() > 1)
                    {
                        fsm->hub->MAX30102_driver().nextSample();
                    }

                    ppgIR = fsm->hub->MAX30102_driver().getFIFOIR();
                    fsm->hub->MAX30102_driver().nextSample();
                    if (ppgIR <= 30000)
                    {
                        fsm->dsp_pro.is_finger_attached = false;
                    }
                    else
                        fsm->dsp_pro.is_finger_attached = true;
                }
            }
            // 3. XỬ LÝ CHUYỂN STATE
            if (fsm->dsp_pro.is_finger_attached)
            {
                // Thông báo ngón tay đã đeo trở lại qua BLE
                somniguard_ble_notify_event(
                    SOMNIGUARD_BLE_EVT_TYPE_SENSOR_STATUS,
                    SOMNIGUARD_BLE_EVT_CODE_FINGER_ATTACHED,
                    (uint16_t)(timestamp_ms - fsm->top_state_entry_ms),
                    0);
                // Khôi phục lại trạng thái ACTIVE_MODE hoặc prev_state
                uint32_t duration_ms = timestamp_ms - fsm->top_state_entry_ms;
                somniguard_top_fsm_state_t target_state = (duration_ms < 5000UL && fsm->prev_top_state != FSM_TOP_INACTIVE)
                                                              ? fsm->prev_top_state
                                                              : FSM_TOP_ACTIVE_MODE;
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = target_state;
                fsm->top_state_entry_ms = timestamp_ms;
                if (target_state == FSM_TOP_ACTIVE_MODE)
                {
                    somniguard_fsm_set_active_state(fsm, SUB_ACTIVE_INIT);
                }
                else if (target_state == FSM_TOP_NORMAL_SLEEP)
                {
                    somniguard_buffer_reset(&fsm->buffer_pro);
                    somniguard_dsp_reset(&fsm->dsp_pro);
                    somniguard_fsm_set_normal_state(fsm, SUB_SLEEP_BUFFERING);
                }
            }
            else if (timestamp_ms - fsm->top_state_entry_ms >= FSM_INACTIVE_TIMEOUT_MS)
            {
                // Quá 60 giây không đeo lại -> Chuyển INACTIVE (Chạy EM4 Shutoff)
                fsm->top_state = FSM_TOP_INACTIVE;
                fsm->inactive_entry_done = false;
            }
            break;
        }

        case FSM_TOP_ACTIVE_MODE:
        {
            somniguard_led_display(FSM_TOP_ACTIVE_MODE);

            // Kiểm tra tuột/tháo ngón tay
            if (!fsm->dsp_pro.is_finger_attached)
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_OFF_FINGER_SUSPEND;
                fsm->top_state_entry_ms = now_ms;
                fsm->off_finger_entry_done = false;

                break;
            }

            if (timestamp_ms - fsm->top_state_entry_ms >= FSM_INACTIVE_TIMEOUT_MS)
            {
                fsm->top_state = FSM_TOP_INACTIVE;
            }
            break;
        }

        case FSM_TOP_NORMAL_SLEEP:
        {
            /**
             * Mode NORMAL SLEEP: Theo dõi khi người dùng ngủ
             * - Thu thập PPG 50Hz
             * - Thu thập IMU 50Hz
             * - Phát hiện bất thường (SpO2 sụt hoặc AI Apnea) -> chuyển DEEP ANALYSIS
             * - Phát hiện thức giấc (cựa quậy 15s) -> chuyển ACTIVE MODE
             * - Tháo thiết bị lâu -> chuyển INACTIVE
             */
            somniguard_led_display(FSM_TOP_NORMAL_SLEEP);
            // Kiểm tra tuột ngón tay
            if (!fsm->dsp_pro.is_finger_attached)
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_OFF_FINGER_SUSPEND;
                fsm->off_finger_entry_done = false;
                fsm->top_state_entry_ms = now_ms;
                break;
            }

            // Kiểm tra người dùng thức giấc (cựa quậy liên tục 5 phút)
            if (fsm->wake_motion_start_ms > 0 &&
                (timestamp_ms - fsm->wake_motion_start_ms >= FSM_WAKE_MOTION_TIME_MS))
            {
                somniguard_fsm_set_top_state(fsm, FSM_TOP_ACTIVE_MODE);
                somniguard_fsm_set_active_state(fsm, SUB_ACTIVE_INIT);
                break;
            }

            // // Phát hiện ngưng thở/bất thường -> DEEP ANALYSIS
            // if (fsm->dsp_res.signal_valid && (fsm->dsp_res.spo2 < 93.0f))
            // {
            //     somniguard_fsm_set_top_state(fsm, FSM_TOP_DEEP_ANALYSIS);
            //     somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_IDLE);
            // }
            break;
        }

        case FSM_TOP_DEEP_ANALYSIS:
        {
            /**
             * Mode DEEP ANALYSIS: Phân tích AI & Can thiệp đa cấp độ
             * - Chạy model AI kết hợp cùng kết quả DSP, Motion để đưa ra can thiệp
             * - Nếu phục hồi tốt -> chuyển NORMAL SLEEP
             * - Nếu không phục hồi -> tăng cấp độ can thiệp (Rung mạnh / BLE + Còi)
             * - Khi người dùng thức dậy / tháo thiết bị lâu -> chuyển INACTIVE
             */
            somniguard_led_display(FSM_TOP_DEEP_ANALYSIS);
            // // Kiểm tra tuột ngón tay
            // if (!fsm->dsp_pro.is_finger_attached)
            // {
            //     somniguard_fsm_set_top_state(fsm, FSM_TOP_OFF_FINGER_SUSPEND);
            //     fsm->off_finger_entry_done = false;
            //     break;
            // }

            // Khi người dùng thức dậy (cựa quậy liên tục 15s) -> chuyển ACTIVE MODE
            if (fsm->wake_motion_start_ms > 0 &&
                (timestamp_ms - fsm->wake_motion_start_ms >= FSM_WAKE_MOTION_TIME_MS))
            {
                fsm->wake_motion_start_ms = 0;
                somniguard_fsm_set_top_state(fsm, FSM_TOP_ACTIVE_MODE);
                somniguard_fsm_set_active_state(fsm, SUB_ACTIVE_INIT);
                break;
            }

            break;
        }
        }
        somniguard_fsm_apply_actuators(fsm);
    }
}

/**
 * @brief Hàm RTOS Task độc lập thực thi FSM trong state DEEP ANALYSIS
 */
void somniguard_deep_analysis_task(void *pvParameters)
{
    somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
    if (fsm == nullptr)
    {
        vTaskDelete(nullptr);
        return;
    }
    printf("--- FSM Deep Analysis Task Started ---\r\n");

    // Biến lưu lại cấp can thiệp vừa thực thi để leo thang bậc thang nếu không hồi phục
    static somniguard_sub_fsm_state_t last_executed_intervention = SUB_INTERVENT_IDLE;
    // Biến theo dõi sub-state trước đó: chỉ gọi apply_actuators khi sub-state vừa thay đổi (entry action)
    static somniguard_sub_fsm_state_t prev_sub_state = SUB_INTERVENT_IDLE;

    while (1)
    {
        if (fsm->top_state == FSM_TOP_DEEP_ANALYSIS)
        {
            uint32_t timestamp_ms = pdTICKS_TO_MS(xTaskGetTickCount());
            uint32_t elapsed_in_sub = timestamp_ms - fsm->sub_state_entry_ms;

            switch (fsm->sub_state)
            {
            case SUB_INTERVENT_IDLE:
                // 1. Chạy AI Model từ bộ đệm Tensor (kèm cơ chế Inference Gating)
                fsm->last_ai_event = AI_EVENT_NORMAL;
                if (fsm->buffer_pro.is_full)
                {
                    // INFERENCE GATING:
                    // Vì model được huấn luyện trên dữ liệu tay đứng im (IMU ≈ 0),
                    // nếu người dùng đang cử động hoặc vừa hết cử động chưa đủ 4s (quiet_duration_ms < 4000),
                    // ta tạm thời khóa suy luận AI để tránh hiện tượng Out-of-Distribution (OOD) gây báo động nhầm.
                    if (fsm->motion_res.is_moving || fsm->quiet_duration_ms < 4000UL)
                    {
                        fsm->last_ai_event = AI_EVENT_NORMAL;
                    }
                    else
                    {
                        fsm->last_ai_event = somniguard_ai_predict(&fsm->buffer_pro);
                    }
                }

                // Phục hồi tốt: SpO2 khôi phục >= 96% VÀ AI xác nhận bình thường -> NORMAL_SLEEP
                if (fsm->dsp_res.signal_valid && fsm->dsp_res.spo2 >= 96.0f && fsm->last_ai_event == AI_EVENT_NORMAL && !fsm->motion_res.is_moving)
                {
                    uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                    fsm->prev_top_state = fsm->top_state;
                    fsm->top_state = FSM_TOP_NORMAL_SLEEP;
                    fsm->top_state_entry_ms = now_ms;

                    // Reset sub-state về BUFFERING để tích lũy lại baseline SpO2 & AC sau can thiệp
                    fsm->normal_state = SUB_SLEEP_BUFFERING;
                    fsm->sleep_buffering_entry_done = false;
                    fsm->anomaly_detect_ms = 0;
                    fsm->anomaly_sustained = false;
                    fsm->ac_history_idx = 0;
                    fsm->ac_history_count = 0;
                    fsm->last_ac_sample_ms = 0;
                    memset(fsm->ac_history, 0, sizeof(fsm->ac_history));
                }
                // 2. Quyết định cấp can thiệp khởi phát ban đầu dựa trên chẩn đoán AI & SpO2.
                //    Dùng else-if để KHÔNG kích hoạt can thiệp khi đã phục hồi ở trên
                else if (fsm->last_ai_event == AI_EVENT_APNEA_CRITICAL && (fsm->dsp_res.spo2 < 90.0f && fsm->dsp_res.signal_valid))
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_MODERATE_VIBRATE);
                }
                else if (fsm->last_ai_event == AI_EVENT_APNEA_CRITICAL && (fsm->dsp_res.spo2 > 90.0f && fsm->dsp_res.spo2 < 93.0f && fsm->dsp_res.signal_valid))
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_MILD_VIBRATE);
                }
                else if (fsm->last_ai_event == AI_EVENT_HYPOPNIA)
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_MILD_VIBRATE);
                }
                else
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_MILD_VIBRATE);
                }
                // else if (fsm->last_ai_event >= AI_EVENT_APNEA_MILD && fsm->dsp_res.signal_valid)
                // {
                //     // AI còn thấy bất thường nhẹ nhưng SpO2 chưa đủ ngưỡng leo thang -> can thiệp nhẹ
                //     somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_MILD_VIBRATE);
                // }
                // else: AI = NORMAL & SpO2 chưa đến 95% -> giữ IDLE chờ buffer mới
                break;

            case SUB_INTERVENT_MILD_VIBRATE:
                // Cấp 1: Rung nhẹ 20% (3 giây)

                last_executed_intervention = SUB_INTERVENT_MILD_VIBRATE;
                fsm->vibrate_level = 1;
                fsm->buzzer_alarm = false;
                fsm->ble_sos_flag = false;

                if (elapsed_in_sub >= (HAPTIC_DURATION_MILD_MS + HAPTIC_DURATION_DELAY_MS))
                {
                    // Tắt actuator ngay tại điểm transition → apply_actuators sẽ thấy vibrate_level=0 khi fire
                    fsm->vibrate_level = 0;
                    fsm->buzzer_alarm = false;
                    fsm->ble_sos_flag = false;
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_EVALUATE_RECOVERY);
                    //  somniguard_dsp_reset(&fsm->dsp_pro);
                    fsm->dsp_res.signal_valid = false;
                }
                break;
            case SUB_INTERVENT_MODERATE_VIBRATE:
                // Cấp 2: Rung trung bình 50% (5 giây)
                last_executed_intervention = SUB_INTERVENT_MODERATE_VIBRATE;
                fsm->vibrate_level = 2;
                fsm->buzzer_alarm = false;
                fsm->ble_sos_flag = false;

                if (elapsed_in_sub >= (HAPTIC_DURATION_MODERATE_MS + HAPTIC_DURATION_DELAY_MS))
                {
                    // Tắt actuator ngay tại điểm transition → apply_actuators sẽ thấy vibrate_level=0 khi fire
                    fsm->vibrate_level = 0;
                    fsm->buzzer_alarm = false;
                    fsm->ble_sos_flag = false;
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_EVALUATE_RECOVERY);
                    // somniguard_dsp_reset(&fsm->dsp_pro);
                    fsm->dsp_res.signal_valid = false;
                }
                break;
            case SUB_INTERVENT_STRONG_VIBRATE:
                // Cấp 2: Rung mạnh 80% (5 giây)
                last_executed_intervention = SUB_INTERVENT_STRONG_VIBRATE;
                fsm->vibrate_level = 3;
                fsm->buzzer_alarm = false;
                fsm->ble_sos_flag = false;

                if (elapsed_in_sub >= (HAPTIC_DURATION_STRONG_MS + HAPTIC_DURATION_DELAY_MS))
                {
                    // Tắt actuator ngay tại điểm transition → apply_actuators sẽ thấy vibrate_level=0 khi fire
                    fsm->vibrate_level = 0;
                    fsm->buzzer_alarm = false;
                    fsm->ble_sos_flag = false;
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_EVALUATE_RECOVERY);
                    // somniguard_dsp_reset(&fsm->dsp_pro);
                    fsm->dsp_res.signal_valid = false;
                }
                break;

            case SUB_INTERVENT_BLE_ALARM:
                // Cấp 3: Nguy cấp (Rung mạnh + Còi báo động + Phát BLE SOS cứu hộ)
                last_executed_intervention = SUB_INTERVENT_BLE_ALARM;
                fsm->vibrate_level = 4;
                fsm->buzzer_alarm = true;
                fsm->ble_sos_flag = true;

                // Nếu người dùng giật mình cựa quậy hoặc SpO2 hồi phục -> chuyển sang đánh giá
                if ((fsm->motion_res.is_moving || (fsm->dsp_res.signal_valid && fsm->dsp_res.spo2 >= 93.0f)) && elapsed_in_sub >= (HAPTIC_DURATION_STRONG_MS + HAPTIC_DURATION_DELAY_MS))
                {
                    // Tắt actuator ngay tại điểm transition → apply_actuators sẽ thấy vibrate_level=0 khi fire
                    // fsm->vibrate_level = 0;
                    // fsm->buzzer_alarm = false;
                    // fsm->ble_sos_flag = false;
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_EVALUATE_RECOVERY);
                    fsm->dsp_res.signal_valid = false;
                }
                break;

            case SUB_INTERVENT_EVALUATE_RECOVERY:
                // Tắt rung để cảm biến theo dõi đáp ứng sinh lý chính xác
                fsm->vibrate_level = 0;
                fsm->buzzer_alarm = false;
                fsm->ble_sos_flag = false;

                if (elapsed_in_sub < HAPTIC_DURATION_DELAY_MS)
                {
                    break;
                }
                // A. Kiểm tra hồi phục thành công (SpO2 >= 95%)
                if (fsm->dsp_res.signal_valid && fsm->dsp_res.spo2 >= 95.0f && !fsm->motion_res.is_moving && elapsed_in_sub >= FSM_EVALUATE_TIMEOUT_MS)
                {
                    // Đã khôi phục thành công -> Về IDLE (Top-FSM sẽ chuyển sang NORMAL_SLEEP)
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_IDLE);
                    somniguard_buffer_reset(&fsm->buffer_pro); // Reset buffer để tích lũy baseline mới
                }
                else if (elapsed_in_sub >= FSM_EVALUATE_INTTERVAL_ADVANCED_MS)
                {
                    if (last_executed_intervention == SUB_INTERVENT_MILD_VIBRATE)
                    {
                        // Cấp 1 nhẹ không hiệu quả -> Leo thang lên Cấp 2 trung bình (Moderate Vibrate)
                        somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_MODERATE_VIBRATE);
                    }
                    else if (last_executed_intervention == SUB_INTERVENT_MODERATE_VIBRATE)
                    {
                        // Cấp 2 trung bình không hiệu quả -> Leo thang lên Cấp 3 mạnh (Strong Vibrate)
                        somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_STRONG_VIBRATE);
                    }
                    else if (last_executed_intervention == SUB_INTERVENT_STRONG_VIBRATE)
                    {
                        // Cấp 3 mạnh không hiệu quả -> Leo thang lên Cấp 4 (BLE Alarm SOS)
                        somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_BLE_ALARM);
                    }
                    else // Đã ở cấp 4 nhưng vẫn chưa hồi phục
                    {
                        // Tiếp tục lặp lại cấp 4 để cảnh báo cứu hộ khẩn cấp
                        somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_BLE_ALARM);
                    }
                }
                break;
            }

            // apply_actuators chỉ được gọi 1 lần khi vừa bước vào sub-state mới (entry action)
            // Tránh restart haptic_motor mỗi 100ms → không bị re-trigger blocking loop
            if (fsm->sub_state != prev_sub_state)
            {
                somniguard_fsm_apply_actuators(fsm);
                prev_sub_state = fsm->sub_state;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Hàm RTOS Task độc lập thực thi FSM trong state ACTIVE MODE
 */
void somniguard_active_mode_task(void *pvParameters)
{
    somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
    if (fsm == nullptr)
        return;
    printf("--- FSM Active Mode Task Started ---\r\n");

    while (1)
    {
        if (fsm->top_state == FSM_TOP_ACTIVE_MODE)
        {
            uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
            uint32_t elapsed_in_sub = now_ms - fsm->sub_state_entry_ms;

            switch (fsm->active_state)
            {
            case SUB_ACTIVE_INIT:
                // Entry action: Chỉ config sensor 1 lần khi mới vào state
                if (!fsm->active_init_done)
                {
                    // Tắt các thiết bị cảnh báo
                    fsm->vibrate_level = 0;
                    fsm->buzzer_alarm = false;
                    fsm->ble_sos_flag = false;

                    // Đặt cấu hình lấy mẫu tiết kiệm pin
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_ACTIVE_HZ; // 25Hz
                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_ACTIVE_HZ; // 1Hz
                    fsm->hub->MAX30102_driver().driver().wakeUp();
                    fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);

                    somniguard_led_display(FSM_TOP_ACTIVE_MODE);
                    fsm->hub->agcAmplitudeLed();
                    // Reset sạch DSP & Buffer để xóa toàn bộ dữ liệu biến thiên/nhiễu trong lúc AGC chỉnh LED
                    somniguard_dsp_reset(&fsm->dsp_pro);

                    fsm->active_init_done = true;
                }
                // AGC đã chạy trong app_init(), không gọi ở đây nữa
                // Sau 1s calib xong -> Chuyển sang WAKEFUL
                if (elapsed_in_sub >= 1000UL)
                {
                    fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    somniguard_fsm_set_active_state(fsm, SUB_ACTIVE_WAKEFUL);
                }
                break;

            case SUB_ACTIVE_WAKEFUL:
                // Config sensor chỉ thực hiện khi chuyển state (trong somniguard_fsm_set_active_state)
                // Không cần gọi setSampleRate mỗi 100ms vì freq không đổi trong state này

                // Nếu người dùng nằm yên liên tục (quiet_duration >= 60s) -> Sang PRE_SLEEP
                if (fsm->quiet_duration_ms >= 60000UL)
                {
                    // Nâng freq khi chuyển sang PRE_SLEEP
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_SLEEP_HZ; // 50Hz
                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_SLEEP_HZ; // 50Hz
                                                                          //  fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    somniguard_fsm_set_active_state(fsm, SUB_ACTIVE_PRE_SLEEP);
                }
                break;

            case SUB_ACTIVE_PRE_SLEEP:
                // Freq đã được set khi entry từ WAKEFUL, không cần gọi lại mỗi 100ms

                // Nếu cựa quậy mạnh trở lại -> Quay về WAKEFUL
                if (fsm->motion_res.is_moving && fsm->wake_motion_start_ms > 0 && (now_ms - fsm->wake_motion_start_ms >= 15000UL))
                {
                    // Hạ freq về ACTIVE khi quay lại WAKEFUL
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_ACTIVE_HZ; // 25Hz
                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_ACTIVE_HZ; // 1Hz
                                                                           // fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    somniguard_fsm_set_active_state(fsm, SUB_ACTIVE_WAKEFUL);
                }
                // Nếu nằm yên đủ 3 phút (FSM_SLEEP_ENTER_TIME_MS) và tín hiệu tốt -> Chuyển NORMAL_SLEEP
                else if (fsm->quiet_duration_ms >= FSM_SLEEP_ENTER_TIME_MS && fsm->dsp_res.signal_valid)
                {
                    fsm->sleep_buffering_entry_done = false; // Reset cho sleep entry
                    somniguard_fsm_set_top_state(fsm, FSM_TOP_NORMAL_SLEEP);
                    somniguard_fsm_set_normal_state(fsm, SUB_SLEEP_BUFFERING);
                }
                break;

            case SUB_ACTIVE_SPOT_CHECK:
                // Config chỉ 1 lần khi entry (freq đã set bởi caller)
                if (elapsed_in_sub >= 30000UL)
                { // Sau 30s đo xong
                    // Hạ freq về ACTIVE khi quay lại WAKEFUL
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_ACTIVE_HZ;
                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_ACTIVE_HZ;
                    //  fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    somniguard_fsm_set_active_state(fsm, SUB_ACTIVE_WAKEFUL);
                }
                break;
            }
            somniguard_fsm_apply_actuators(fsm);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Hàm RTOS Task độc lập thực thi FSM trong state NORMAL SLEEP
 *
 * Nhiệm vụ:
 *   1. SUB_SLEEP_BUFFERING  — Chờ tensor buffer đầy 40s, sau đó chuyển MONITORING.
 *   2. SUB_SLEEP_MONITORING — Theo dõi liên tục 2 ngưỡng bất thường:
 *        Ngưỡng 1 (cứng): SpO2 < FSM_SPO2_CRITICAL_THRESHOLD (93%) → DEEP_ANALYSIS ngay
 *        Ngưỡng 2 (mềm) : SpO2 giảm >= APNEA_DROP_THRESHOLD (4%) so với baseline
 *                          và kéo dài >= FSM_ANOMALY_SUSTAIN_MS (10s) → DEEP_ANALYSIS
 *
 * Lưu ý: AI KHÔNG được gọi ở đây. Việc gọi AI chỉ xảy ra trong DEEP_ANALYSIS.
 */
void somniguard_normal_sleep_task(void *pvParameters)
{
    somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
    if (fsm == nullptr)
        return;
    printf("--- FSM Normal Sleep Task Started ---\r\n");

    while (1)
    {
        if (fsm->top_state == FSM_TOP_NORMAL_SLEEP)
        {
            uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());

            switch (fsm->normal_state)
            {
            // =================================================================
            // SUB_SLEEP_BUFFERING: Tích lũy buffer Tensor 40s
            // Chờ DataProcessingTask nạp đủ 1000 mẫu vào buffer_pro trước khi
            // chuyển sang MONITORING. Không can thiệp vào luồng data.
            // =================================================================
            case SUB_SLEEP_BUFFERING:
                // Entry action: Config sensor 1 lần
                if (!fsm->sleep_buffering_entry_done && fsm->prev_top_state != FSM_TOP_DEEP_ANALYSIS)
                {
                    // Hiệu ứng LED nhấp nháy báo bắt đầu vào chế độ đo ngủ ban đêm rồi tắt hẳn
                    somniguard_led_sleep_buffering_start();

                    fsm->vibrate_level = 0;
                    fsm->buzzer_alarm = false;
                    fsm->ble_sos_flag = false;

                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_SLEEP_HZ;
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_SLEEP_HZ;

                    //   fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    fsm->sleep_buffering_entry_done = true;
                    // agc current led before sleep

                    printf("[SLEEP] Buffering... waiting for %d samples.\r\n", TENSOR_MAX_ROWS);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }

                // // [BẢO VỆ FAST-PATH TRONG LÚC NẠP BUFFER]
                // if (fsm->dsp_res.signal_valid && !fsm->motion_res.is_moving)
                // {
                //     if (fsm->dsp_res.spo2 < FSM_SPO2_CRITICAL_THRESHOLD) // < 90%
                //     {
                //         somniguard_fsm_set_top_state(fsm, FSM_TOP_DEEP_ANALYSIS);
                //         somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_MILD_VIBRATE);
                //         break;
                //     }
                //     else if (fsm->dsp_res.spo2 < FSM_SPO2_WARN_THRESHOLD) // < 93%
                //     {
                //         somniguard_fsm_set_top_state(fsm, FSM_TOP_DEEP_ANALYSIS);
                //         somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_MILD_VIBRATE);
                //         break;
                //     }
                // }

                // Chờ buffer đầy đủ 40s (1000 mẫu) mới chuyển sang MONITORING
                if (fsm->buffer_pro.is_full)
                {
                    // Chụp baseline SpO2 tại thời điểm bắt đầu MONITORING
                    fsm->spo2_baseline = fsm->dsp_res.signal_valid ? fsm->dsp_res.spo2 : 98.0f;
                    fsm->anomaly_detect_ms = 0;
                    fsm->anomaly_sustained = false;
                    fsm->ac_history_idx = 0;
                    fsm->ac_history_count = 0;
                    fsm->last_ac_sample_ms = 0;
                    memset(fsm->ac_history, 0, sizeof(fsm->ac_history));

                    // printf("[SLEEP] Buffer full (%d samples). Starting monitoring. SpO2 baseline: %d%%\r\n",
                    //        TENSOR_MAX_ROWS, (int)fsm->spo2_baseline);

                    somniguard_fsm_set_normal_state(fsm, SUB_SLEEP_MONITORING);
                }
                break;

            // =================================================================
            // SUB_SLEEP_MONITORING: Theo dõi bất thường liên tục
            // =================================================================
            case SUB_SLEEP_MONITORING:
            {
                // Bỏ qua nếu tín hiệu PPG chưa ổn định
                if (!fsm->dsp_res.signal_valid)
                    break;

                float spo2 = fsm->dsp_res.spo2;
                // float ac_current = fsm->dsp_res.ac_ir;

                // // --- 1. Cập nhật bộ đệm 10 giây lưu vết RMS AC (mỗi 1000ms lấy 1 mẫu) ---
                // if (now_ms - fsm->last_ac_sample_ms >= 1000UL && fsm->dsp_res.signal_valid)
                // {
                //     fsm->last_ac_sample_ms = now_ms;
                //     fsm->ac_history[fsm->ac_history_idx] = ac_current;
                //     fsm->ac_history_idx = (fsm->ac_history_idx + 1) % 10;
                //     if (fsm->ac_history_count < 10)
                //     {
                //         fsm->ac_history_count++;
                //     }
                // }

                // --- Ngưỡng 1 (Cấp bách): SpO2 tụt dưới 90% khi nằm yên ---
                if (spo2 < FSM_SPO2_CRITICAL_THRESHOLD && !fsm->motion_res.is_moving)
                {
                    fsm->anomaly_detect_ms = 0;
                    fsm->anomaly_sustained = false;
                    somniguard_fsm_set_top_state(fsm, FSM_TOP_DEEP_ANALYSIS);
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_STRONG_VIBRATE);
                    printf("Thresshold 1\n");
                    break;
                }

                // --- Ngưỡng 2 (Cứng): SpO2 tụt dưới 93% ---
                // Nguy cơ ngưng thở rõ ràng → chuyển DEEP_ANALYSIS ngay lập tức
                if (spo2 < FSM_SPO2_WARN_THRESHOLD && !fsm->motion_res.is_moving)
                {
                    // printf("[SLEEP] ANOMALY Tier-1: SpO2 %.1f%% < %.0f%% threshold! -> DEEP_ANALYSIS\r\n",
                    //        spo2, FSM_SPO2_WARN_THRESHOLD);

                    fsm->anomaly_detect_ms = 0;
                    fsm->anomaly_sustained = false;
                    somniguard_fsm_set_top_state(fsm, FSM_TOP_DEEP_ANALYSIS);
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_IDLE);
                    printf("Thresshold 2\n");
                    break;
                }

                // --- Ngưỡng 3 (Cảnh báo sớm - Co mạch ngoại vi do giao cảm): AC Drop >= 35% trong cửa sổ 10 giây ---
                // // Nguồn lâm sàng: JCSM & PAT studies (thresshold_hospital.md:L26-L29)
                // // So sánh AC hiện tại với AC ở 10 giây trước (mẫu cũ nhất trong vòng tròn)
                // if (fsm->ac_history_count >= 10 && !fsm->motion_res.is_moving)
                // {
                //     // Vị trí ac_history_idx hiện tại chính là con trỏ tới mẫu cũ nhất ghi cách đây 10 giây
                //     float ac_10s_ago = fsm->ac_history[fsm->ac_history_idx];
                //     if (ac_10s_ago > 1.0f)
                //     {
                //         float ac_drop_10s_pct = ((ac_10s_ago - ac_current) / ac_10s_ago) * 100.0f;
                //         if (ac_drop_10s_pct >= AC_AMP_DROP_THRESHOLD_PCT) // >= 35% trong 10 giây
                //         {
                //             // printf("[SLEEP] ANOMALY Tier-Early: AC Drop %.1f%% in 10s (%.1f -> %.1f) -> DEEP_ANALYSIS\r\n",
                //             //        ac_drop_10s_pct, ac_10s_ago, ac_current);

                //             fsm->anomaly_detect_ms = 0;
                //             fsm->anomaly_sustained = false;
                //             somniguard_fsm_set_top_state(fsm, FSM_TOP_DEEP_ANALYSIS);
                //             somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_IDLE);
                //             printf("Thresshold 3\n");
                //             break;
                //         }
                //     }
                // }

                // --- Ngưỡng 4 (Mềm): SpO2 drop >= 4% so với baseline, kéo dài >= 10s ---
                // Phát hiện xu hướng giảm oxy máu chậm (hypopnea / mild apnea)
                float spo2_drop = fsm->spo2_baseline - spo2;
                if (spo2_drop >= APNEA_DROP_THRESHOLD)
                {
                    if (fsm->anomaly_detect_ms == 0)
                    {
                        // Bắt đầu đếm thời gian bất thường bền vững
                        fsm->anomaly_detect_ms = now_ms;
                        fsm->anomaly_sustained = true;
                        // printf("[SLEEP] ANOMALY Tier-2: SpO2 drop %.1f%% (baseline %.1f%% -> now %.1f%%). Counting...\r\n",
                        //        spo2_drop, fsm->spo2_baseline, spo2);
                    }
                    else if ((now_ms - fsm->anomaly_detect_ms) >= FSM_ANOMALY_SUSTAIN_MS)
                    {
                        // Drop đã kéo dài đủ 10s → chuyển DEEP_ANALYSIS
                        // printf("[SLEEP] ANOMALY Tier-2: Sustained %.lu ms (>= %lu ms). -> DEEP_ANALYSIS\r\n",
                        //        (unsigned long)(now_ms - fsm->anomaly_detect_ms),
                        //        (unsigned long)FSM_ANOMALY_SUSTAIN_MS);

                        fsm->anomaly_detect_ms = 0;
                        fsm->anomaly_sustained = false;
                        somniguard_fsm_set_top_state(fsm, FSM_TOP_DEEP_ANALYSIS);
                        somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_IDLE);
                        printf("Thresshold 4\n");
                        break;
                    }
                }
                else
                {
                    // SpO2 trở về bình thường → reset bộ đếm bất thường
                    if (fsm->anomaly_sustained)
                    {
                        // printf("[SLEEP] Anomaly cleared. SpO2 recovered to %.1f%%\r\n", spo2);
                    }
                    fsm->anomaly_detect_ms = 0;
                    fsm->anomaly_sustained = false;

                    // Cập nhật baseline theo chiều tăng (slow-tracking upward only)
                    // Cho phép baseline phản ánh SpO2 tốt hơn nếu bệnh nhân hồi phục
                    if (spo2 > fsm->spo2_baseline)
                    {
                        fsm->spo2_baseline = spo2;
                    }
                }

                break;
            }

            default:
                break;
            }

            somniguard_fsm_apply_actuators(fsm);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void somniguard_fsm_apply_actuators(somniguard_fsm_t *fsm)
{
    // 3. Điều khiển BLE SOS
    if (fsm->ble_sos_flag)
    {
        somniguard_BLE_control(); // Phát gói tin BLE SOS khẩn cấp
    }
    // 1. Điều khiển Motor Rung (Haptic)
    switch (fsm->vibrate_level)
    {
    case 0:
        somniguard_haptic_motor(0, 0);
        break; // Tắt rung
    case 1:
        somniguard_haptic_motor(HAPTIC_PWM_MILD, HAPTIC_DURATION_MILD_MS);
        break; // Rung nhẹ (Ví dụ 100/255 trong 3s)
    case 2:
        somniguard_haptic_motor(HAPTIC_PWM_MODERATE, HAPTIC_DURATION_MODERATE_MS);
        break; // Rung trung bình (128/255 trong 5s)
    case 3:
        somniguard_haptic_motor(HAPTIC_PWM_STRONG, HAPTIC_DURATION_STRONG_MS);
        break; // Rung mạnh (255/255 trong 7s)
    case 4:
        somniguard_haptic_motor(HAPTIC_PWM_STRONG, 20000U);
        break; // Rung cực mạnh (255/255 trong 20s)
    }
}

#include "em_emu.h"
#include "em_gpio.h"

void somniguard_enter_em4_shutoff(somniguard_fsm_t *fsm)
{
    //   printf("\r\n[EMU POWER] Entering EM4 Shutoff Mode via EMLIB...\r\n");

    // Phát gói tin BLE báo chuẩn bị tắt nguồn
    somniguard_ble_notify_event(
        SOMNIGUARD_BLE_EVT_TYPE_POWER_SYSTEM,
        SOMNIGUARD_BLE_EVT_CODE_EM4_SHUTOFF,
        0, 0);

    // 1. Tắt các thiết bị ngoại vi & cảm biến (MAX30102)
    if (fsm != NULL)
    {
        somniguard_power_off(fsm);
        if (fsm->hub != NULL)
        {
            // fsm->hub->MAX30102_driver().shutDown();
        }
    }

    // 2. Cấu hình chân nút bấm (VD: Chân Pin 4) làm ngắt EM4 Wakeup Pin
    // Khi nhấn nút, MCU sẽ tự động tỉnh dậy từ EM4 và Reset thiết bị
    GPIO_EM4WUExtIntConfig(gpioPortB, 3, 4, false, true);

    // 3. Cấu hình thông số EM4 (Tắt Unretained RAM để tiết kiệm pin tối đa ~100nA)
    EMU_EM4Init_TypeDef em4Init = EMU_EM4INIT_DEFAULT;
    em4Init.retainLfxo = false;
    em4Init.em4State = emuEM4Shutoff; // Mức Shutoff tiết kiệm pin nhất
    EMU_EM4Init(&em4Init);

    // 4. Lệnh ép MCU nhảy thẳng vào EM4 Shutoff
    EMU_EnterEM4();
}
