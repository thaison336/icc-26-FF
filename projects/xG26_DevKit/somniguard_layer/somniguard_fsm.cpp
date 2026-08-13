#include "somniguard_fsm.h"
#include "ble_notification_manager.h"
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

    // Khởi tạo các cờ entry action (memset đã set = false)
    fsm->off_finger_entry_done = false;
    fsm->active_init_done = false;
    fsm->sleep_buffering_entry_done = false;

    // printf("\r\n[FSM INIT] SomniGuard FSM initialized. Start TopState: %s, ActiveSubState: %s\r\n",
    //        somniguard_top_state_str(fsm->top_state),
    //        somniguard_active_state_str(fsm->active_state));
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
    fsm->prev_top_state = fsm->top_state;
    fsm->top_state = new_state;
    fsm->top_state_entry_ms = now_ms;

    // Phát gói tin BLE thông báo chuyển trạng thái hệ thống
    somniguard_ble_notify_event(
        SOMNIGUARD_BLE_EVT_TYPE_POWER_SYSTEM,
        SOMNIGUARD_BLE_EVT_CODE_FSM_STATE_CHG,
        (uint16_t)fsm->prev_top_state,
        (uint16_t)new_state
    );
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

    if (frame.count == 0)
    {
        return AI_EVENT_NORMAL;
    }

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

    // Quy tắc phán đoán chẩn đoán mô hình AI (AI Diagnosis Engine Logic):
    if (min_spo2 < 80.0f || spo2_drop >= 8.0f)
    {
        return AI_EVENT_APNEA_CRITICAL;
    }
    if (min_spo2 < 88.0f || spo2_drop >= 5.0f)
    {
        return AI_EVENT_APNEA_SEVERE;
    }
    if (min_spo2 < 92.0f || spo2_drop >= 3.0f)
    {
        return AI_EVENT_APNEA_MILD;
    }
    if (mean_spo2 < 94.0f)
    {
        return AI_EVENT_HYPOPNIA;
    }

    return AI_EVENT_NORMAL;
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

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // Thực thi 100ms một lần (10Hz)

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
            // 1. Nằm yên: Cập nhật độ dài thời gian nằm yên liên tục
            fsm->quiet_duration_ms = timestamp_ms - fsm->last_motion_time_ms;
            // 2. Nằm yên trở lại: Reset cờ đếm thức giấc
            fsm->wake_motion_start_ms = 0;
        }

        // =========================================================================
        // TOP-LEVEL FSM TRANSITIONS (TẦNG 1)
        // =========================================================================
        switch (fsm->top_state)
        {
        case FSM_TOP_INACTIVE:
        { // Trang thai khong hoat dong (tat nguon thiet bi)
            // Thiet bi chi duoc bat lai khi nguoi dung nhan nut nguon (chuyển top_state sang ACTIVE_MODE)
            somniguard_led_display(FSM_TOP_INACTIVE);

            fsm->vibrate_level = 0;
            fsm->buzzer_alarm = false;
            fsm->ble_sos_flag = false;
            fsm->requested_imu_freq = 0;
            fsm->requested_ppg_freq = 0;

            vTaskDelay(pdMS_TO_TICKS(5000)); // hiện thị led biểu thị trạng thái thiết bị chuẩn bị tắt
            // ham thuc hien BLE thong bao cho app tren dt bt thiet bi da tat
            // ham thuc hien tat nguon
            somniguard_enter_em4_shutoff(fsm);
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
                    0
                );
                if (fsm->hub != nullptr)
                {
                    fsm->hub->MAX30102_driver().setPulseAmplitudeRed(0);
                    fsm->hub->MAX30102_driver().setPulseAmplitudeIR(0);
                    fsm->hub->MAX30102_driver().driver().shutDown();
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
                    0
                );
                // Khôi phục lại trạng thái ACTIVE_MODE hoặc prev_state
                uint32_t duration_ms = timestamp_ms - fsm->top_state_entry_ms;
                somniguard_top_fsm_state_t target_state = (duration_ms < 5000UL && fsm->prev_top_state != FSM_TOP_INACTIVE)
                                                              ? fsm->prev_top_state
                                                              : FSM_TOP_ACTIVE_MODE;
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = target_state;
                fsm->top_state_entry_ms = timestamp_ms;
                fsm->off_finger_entry_done = false;
                fsm->active_init_done = false;
            }
            else if (timestamp_ms - fsm->top_state_entry_ms >= 60000UL)
            {
                // Quá 60 giây không đeo lại -> Chuyển INACTIVE (Chạy EM4 Shutoff)
                fsm->top_state = FSM_TOP_INACTIVE;
                fsm->off_finger_entry_done = false;
            }
            break;
        }

        case FSM_TOP_ACTIVE_MODE:
        {
            /**
             * Mode ACTIVE: Khi người dùng còn thức / hoạt động ban ngày
             * - Hiển thị trạng thái LED
             * - Đo IMU 25Hz để biết người dùng đã ngủ hay còn vận động
             * - Đo PPG 1Hz kiểm tra đeo thiết bị
             * - Đạt điều kiện không cử động 3 phút -> chuyển NORMAL SLEEP
             */
            somniguard_led_display(FSM_TOP_ACTIVE_MODE);
            // Kiểm tra tuột/tháo ngón tay
            if (!fsm->dsp_pro.is_finger_attached)
            {

                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_OFF_FINGER_SUSPEND;
                fsm->top_state_entry_ms = now_ms;
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
                fsm->top_state_entry_ms = now_ms;
                break;
            }

            // Kiểm tra người dùng thức giấc (cựa quậy liên tục 15s)
            if (fsm->wake_motion_start_ms > 0 &&
                (timestamp_ms - fsm->wake_motion_start_ms >= FSM_WAKE_MOTION_TIME_MS))
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_ACTIVE_MODE;
                fsm->top_state_entry_ms = now_ms;
            }

            // Phát hiện ngưng thở/bất thường -> DEEP ANALYSIS
            if (fsm->dsp_res.signal_valid && (fsm->dsp_res.spo2 < 93.0f))
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_DEEP_ANALYSIS;
                fsm->top_state_entry_ms = now_ms;
            }
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
            // Kiểm tra tuột ngón tay
            if (!fsm->dsp_pro.is_finger_attached)
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_OFF_FINGER_SUSPEND;
                fsm->top_state_entry_ms = now_ms;
                break;
            }

            // Khi người dùng thức dậy (cựa quậy liên tục 15s) -> chuyển ACTIVE MODE
            if (fsm->wake_motion_start_ms > 0 &&
                (timestamp_ms - fsm->wake_motion_start_ms >= FSM_WAKE_MOTION_TIME_MS))
            {
                fsm->wake_motion_start_ms = 0;
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_ACTIVE_MODE;
                fsm->top_state_entry_ms = now_ms;
                break;
            }

            // Phục hồi tốt: SpO2 khôi phục >= 95% và sub_state về IDLE -> NORMAL_SLEEP
            if (fsm->dsp_res.signal_valid && fsm->dsp_res.spo2 >= 95.0f && fsm->sub_state == SUB_INTERVENT_IDLE)
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_NORMAL_SLEEP;
                fsm->top_state_entry_ms = now_ms;

                // Reset sub-state về BUFFERING để tích lũy lại baseline SpO2 sau can thiệp
                fsm->normal_state = SUB_SLEEP_BUFFERING;
                fsm->sleep_buffering_entry_done = false;
                fsm->anomaly_detect_ms = 0;
                fsm->anomaly_sustained = false;
            }
            break;
        }
        }
        somniguard_fsm_apply_actuators(fsm);
    }
}

/**
 * @brief Ham RTOS Task độc lập thực thi FSM trong state DEEP ANALYSIS
 */
void somniguard_deep_analysis_task(void *pvParameters)
{
    somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
    if (fsm == nullptr)
    {
        vTaskDelete(nullptr);
        return;
    }

    while (1)
    {
        if (fsm->top_state == FSM_TOP_DEEP_ANALYSIS)
        {
            uint32_t timestamp_ms = pdTICKS_TO_MS(xTaskGetTickCount());
            uint32_t elapsed_in_sub = timestamp_ms - fsm->sub_state_entry_ms;

            switch (fsm->sub_state)
            {
            case SUB_INTERVENT_IDLE:
                // Đánh giá mức độ nghi ngờ để chọn cấp độ can thiệp ban đầu
                // Cập nhật kết quả chẩn đoán AI
                if (fsm->buffer_pro.is_full)
                {
                    fsm->last_ai_event = somniguard_ai_predict(&fsm->buffer_pro);
                }

                if (fsm->last_ai_event == AI_EVENT_APNEA_CRITICAL || fsm->dsp_res.spo2 < 82.0f)
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_BLE_ALARM);
                }
                else if (fsm->last_ai_event == AI_EVENT_APNEA_SEVERE || fsm->dsp_res.spo2 < 88.0f)
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_STRONG_VIBRATE);
                }
                else if (fsm->last_ai_event == AI_EVENT_APNEA_MILD || fsm->last_ai_event == AI_EVENT_HYPOPNIA || fsm->dsp_res.spo2 < 93.0f)
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_MILD_VIBRATE);
                }
                break;

            case SUB_INTERVENT_MILD_VIBRATE:
                // Rung nhẹ cấp 1 (3 giây)
                fsm->vibrate_level = 1;
                fsm->buzzer_alarm = false;
                fsm->ble_sos_flag = false;

                if (elapsed_in_sub >= FSM_MILD_VIB_DURATION_MS)
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_EVALUATE_RECOVERY);
                }
                break;

            case SUB_INTERVENT_STRONG_VIBRATE:
                // Rung mạnh cấp 2 (5 giây)
                fsm->vibrate_level = 2;
                fsm->buzzer_alarm = false;

                if (elapsed_in_sub >= FSM_STRONG_VIB_DURATION_MS)
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_EVALUATE_RECOVERY);
                }
                break;

            case SUB_INTERVENT_BLE_ALARM:
                // Nguy cấp: Rung mạnh + Còi báo động + Phát BLE SOS cứu hộ
                fsm->vibrate_level = 2;
                fsm->buzzer_alarm = true;
                fsm->ble_sos_flag = true;

                // Nếu người dùng giật mình cựa quậy hoặc SpO2 hồi phục -> chuyển sang đánh giá
                if (fsm->motion_res.is_moving || (fsm->dsp_res.signal_valid && fsm->dsp_res.spo2 >= 90.0f))
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_EVALUATE_RECOVERY);
                }
                break;

            case SUB_INTERVENT_EVALUATE_RECOVERY:
                // Tắt rung để theo dõi đáp ứng sinh lý
                fsm->vibrate_level = 0;
                fsm->buzzer_alarm = false;
                fsm->ble_sos_flag = false;
                // Đánh giá chỉ số phục hồi sau can thiệp (10 giây)
                if (fsm->dsp_res.signal_valid && fsm->dsp_res.spo2 >= 95.0f)
                {
                    // Đã khôi phục thành công -> Về IDLE (Top-FSM sẽ chuyển sang NORMAL_SLEEP)
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_IDLE);
                }
                else if (elapsed_in_sub >= FSM_EVALUATE_TIMEOUT_MS)
                {
                    // Hết thời gian đánh giá mà chưa khôi phục -> Tăng cấp độ can thiệp
                    if (fsm->last_ai_event == AI_EVENT_APNEA_CRITICAL || fsm->dsp_res.spo2 < 85.0f)
                    {
                        somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_BLE_ALARM);
                    }
                    else
                    {
                        somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_STRONG_VIBRATE);
                    }
                }
                break;
            }
            somniguard_fsm_apply_actuators(fsm);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 *@brief Ham RTOS Task độc lập thực thi FSM trong state ACTIVE MODE
 */
void somniguard_active_mode_task(void *pvParameters)
{

    somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
    if (fsm == nullptr)
        return;

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
                    //   fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    fsm->active_init_done = true;
                }
                // AGC đã chạy trong app_init(), không gọi ở đây nữa
                // Sau 1s calib xong -> Chuyển sang WAKEFUL
                if (elapsed_in_sub >= 1000)
                {
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
                if (fsm->motion_res.is_moving)
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
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_IDLE);
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
 * @brief Ham RTOS Task độc lập thực thi FSM trong state NORMAL SLEEP
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
                    fsm->vibrate_level = 0;
                    fsm->buzzer_alarm = false;
                    fsm->ble_sos_flag = false;

                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_SLEEP_HZ;
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_SLEEP_HZ;

                    //   fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    fsm->sleep_buffering_entry_done = true;

                    // printf("[SLEEP] Buffering... waiting for %d samples.\r\n", TENSOR_MAX_ROWS);
                }

                // Chờ buffer đầy đủ 40s (1000 mẫu) mới chuyển sang MONITORING
                if (fsm->buffer_pro.is_full)
                {
                    // Chụp baseline SpO2 tại thời điểm bắt đầu MONITORING
                    fsm->spo2_baseline = fsm->dsp_res.signal_valid ? fsm->dsp_res.spo2 : 98.0f;
                    fsm->anomaly_detect_ms = 0;
                    fsm->anomaly_sustained = false;

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

                // --- Ngưỡng 1 (Cứng): SpO2 tụt dưới 93% ---
                // Nguy cơ ngưng thở rõ ràng → chuyển DEEP_ANALYSIS ngay lập tức
                if (spo2 < FSM_SPO2_CRITICAL_THRESHOLD)
                {
                    // printf("[SLEEP] ANOMALY Tier-1: SpO2 %.1f%% < %.0f%% threshold! -> DEEP_ANALYSIS\r\n",
                    //        spo2, FSM_SPO2_CRITICAL_THRESHOLD);

                    fsm->anomaly_detect_ms = 0;
                    fsm->anomaly_sustained = false;
                    somniguard_fsm_set_top_state(fsm, FSM_TOP_DEEP_ANALYSIS);
                    break;
                }

                // --- Ngưỡng 2 (Mềm): SpO2 drop >= 4% so với baseline, kéo dài >= 10s ---
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
    // 1. Điều khiển Motor Rung (Haptic)
    switch (fsm->vibrate_level)
    {
    case 0:
        somniguard_haptic_motor(0, 0);
        break; // Tắt rung
    case 1:
        somniguard_haptic_motor(100, 3000);
        break; // Rung nhẹ (Ví dụ 100/255 trong 3s)
    case 2:
        somniguard_haptic_motor(255, 5000);
        break; // Rung mạnh (255/255 trong 5s)
    }
    // 2. Điều khiển Còi Báo Động (Buzzer)
    if (fsm->buzzer_alarm)
    {
        // Gọi hàm bật còi PWM/GPIO (VD: buzzer_on())
    }
    else
    {
        // Gọi hàm tắt còi (VD: buzzer_off())
    }
    // 3. Điều khiển BLE SOS
    if (fsm->ble_sos_flag)
    {
        somniguard_BLE_control(); // Phát gói tin BLE SOS khẩn cấp
    }
}

#include "em_emu.h"
#include "em_gpio.h"

void somniguard_enter_em4_shutoff(somniguard_fsm_t *fsm)
{
    // printf("\r\n[EMU POWER] Entering EM4 Shutoff Mode via EMLIB...\r\n");

    // Phát gói tin BLE báo chuẩn bị tắt nguồn
    somniguard_ble_notify_event(
        SOMNIGUARD_BLE_EVT_TYPE_POWER_SYSTEM,
        SOMNIGUARD_BLE_EVT_CODE_EM4_SHUTOFF,
        0, 0
    );

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
