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

    // Reset toÃ n bá»™ struct fsm vá» 0
    memset(fsm, 0, sizeof(somniguard_fsm_t));
    fsm->hub = hub;
    // Khá»Ÿi táº¡o cÃ¡c tráº¡ng thÃ¡i FSM ban Ä‘áº§u
    fsm->top_state = FSM_TOP_ACTIVE_MODE;
    fsm->prev_top_state = FSM_TOP_INACTIVE;
    fsm->sub_state = SUB_INTERVENT_IDLE;
    fsm->active_state = SUB_ACTIVE_INIT;
    fsm->normal_state = SUB_SLEEP_BUFFERING;

    uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    fsm->top_state_entry_ms = now_ms;
    fsm->sub_state_entry_ms = now_ms;
    fsm->last_motion_time_ms = now_ms;

    // Khá»Ÿi táº¡o cÃ¡c bá»™ xá»­ lÃ½ dá»¯ liá»‡u con (DSP, Motion & Buffer)
    somniguard_dsp_init(&fsm->dsp_pro);
    somniguard_buffer_init(&fsm->buffer_pro);
    somniguard_motion_init(&fsm->motion_pro, IMU_SAMPLING_RATE_ACTIVE_HZ);

    // Khá»Ÿi táº¡o cÃ¡c cá»  káº¿t quáº£ vÃ  cá»  Ä‘iá» u khiá»ƒn ngoáº¡i vi máº·c Ä‘á»‹nh
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

    // Khá»Ÿi táº¡o cÃ¡c cá»  entry action (memset Ä‘Ã£ set = false)
    fsm->off_finger_entry_done = false;
    fsm->active_init_done = false;
    fsm->sleep_buffering_entry_done = false;

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

    // PhÃ¡t gÃ³i tin BLE thÃ´ng bÃ¡o chuyá»ƒn tráº¡ng thÃ¡i há»‡ thá»‘ng
    somniguard_ble_notify_event(
        SOMNIGUARD_BLE_EVT_TYPE_POWER_SYSTEM,
        SOMNIGUARD_BLE_EVT_CODE_FSM_STATE_CHG,
        (uint16_t)fsm->prev_top_state,
        (uint16_t)new_state);
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

    // Reset entry flag khi quay láº¡i BUFFERING Ä‘á»ƒ entry-action cháº¡y láº¡i
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

    // Táº¯t cÃ¡c ngoáº¡i vi cháº¥p hÃ nh
    fsm->vibrate_level = 0;
    fsm->buzzer_alarm = false;
    fsm->ble_sos_flag = false;
    fsm->requested_imu_freq = 0;
    fsm->requested_ppg_freq = 0;

    // 2. Táº¯t dÃ²ng LED vÃ  Shutdown MAX30102
    if (fsm->hub != nullptr)
    {
        fsm->hub->MAX30102_driver().setPulseAmplitudeRed(0);
        fsm->hub->MAX30102_driver().setPulseAmplitudeIR(0);
        fsm->hub->MAX30102_driver().driver().shutDown(); // <-- Gá»­i lá»‡nh Shutdown I2C
    }

    // Reset bá»™ Ä‘á»‡m & thuáº­t toÃ¡n
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
    float prob = predict_window_confidence((const float *)frame.data);

    // Trường hợp mô hình AI chưa khởi tạo hoặc lỗi, dùng quy tắc an toàn dựa vào SpO2 dự phòng
    if (prob < 0.0f)
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
    if (prob >= 0.85f)
    {
        event = AI_EVENT_APNEA_CRITICAL;
    }
    else if (prob >= 0.65f)
    {
        event = AI_EVENT_APNEA_SEVERE;
    }
    else if (prob >= 0.50f)
    {
        event = AI_EVENT_APNEA_MILD;
    }
    else if (prob >= 0.35f)
    {
        event = AI_EVENT_HYPOPNIA;
    }
    else
    {
        event = AI_EVENT_NORMAL;
    }

    printf("[AI DIAGNOSIS] Model Confidence: %.4f -> Event: %s\r\n", prob, somniguard_ai_event_str(event));
    fflush(stdout);

    return event;
}

/**
 * @brief FREERTOS TASK WRAPPER CHO Bá»˜ NÃƒO FSM
 * Thá»±c thi Bá»™ NÃ£o FSM Ä‘á»™c láº­p Ä‘á»‹nh ká»³ (100ms / 10Hz)
 * @param pvParameters Con trá» tá»›i struct somniguard_fsm_t
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
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // Thá»±c thi 100ms má»™t láº§n (10Hz)
    printf("hehehe");
    while (1)
    {
        // Chá» chÃ­nh xÃ¡c 100ms Ä‘á»ƒ cháº¡y vÃ²ng láº·p FSM Ä‘á»‹nh ká»³
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        uint32_t timestamp_ms = pdTICKS_TO_MS(xTaskGetTickCount());

        // =========================================================================
        // Cáº¬P NHáº¬T CÃC Bá»˜ Äáº¾M THá»œI GIAN Cá»¬ Äá»˜NG / Náº°M YÃŠN (MOTIONS & QUIET TIMERS)
        // =========================================================================
        if (fsm->motion_res.is_moving)
        {
            // 1. CÃ³ cá»±a quáº­y: Reset thá»i gian náº±m yÃªn vá» 0
            fsm->last_motion_time_ms = timestamp_ms;
            fsm->quiet_duration_ms = 0;
            // 2. Ghi nháº­n thá»i Ä‘iá»ƒm báº¯t Ä‘áº§u cá»±a quáº­y Ä‘á»ƒ Ä‘áº¿m chu ká»³ thá»©c giáº¥c (15s cá»±a quáº­y liÃªn tá»¥c)
            if (fsm->wake_motion_start_ms == 0)
            {
                fsm->wake_motion_start_ms = timestamp_ms;
            }
        }
        else
        {
            // 1. Náº±m yÃªn: Cáº­p nháº­t Ä‘á»™ dÃ i thá»i gian náº±m yÃªn liÃªn tá»¥c
            fsm->quiet_duration_ms = timestamp_ms - fsm->last_motion_time_ms;
            // 2. Náº±m yÃªn trá»Ÿ láº¡i: Reset cá» Ä‘áº¿m thá»©c giáº¥c
            fsm->wake_motion_start_ms = 0;
        }

        // =========================================================================
        // TOP-LEVEL FSM TRANSITIONS (Táº¦NG 1)
        // =========================================================================
        switch (fsm->top_state)
        {
        case FSM_TOP_INACTIVE:
        { // Trang thai khong hoat dong (tat nguon thiet bi)
            // Thiet bi chi duoc bat lai khi nguoi dung nhan nut nguon (chuyá»ƒn top_state sang ACTIVE_MODE)
            somniguard_led_display(FSM_TOP_INACTIVE);

            fsm->vibrate_level = 0;
            fsm->buzzer_alarm = false;
            fsm->ble_sos_flag = false;
            fsm->requested_imu_freq = 0;
            fsm->requested_ppg_freq = 0;

            vTaskDelay(pdMS_TO_TICKS(5000)); // hiá»‡n thá»‹ led biá»ƒu thá»‹ tráº¡ng thÃ¡i thiáº¿t bá»‹ chuáº©n bá»‹ táº¯t
            // ham thuc hien BLE thong bao cho app tren dt bt thiet bi da tat
            // ham thuc hien tat nguon
            // somniguard_enter_em4_shutoff(fsm);
            break;
        }
        case FSM_TOP_OFF_FINGER_SUSPEND:
        {
            // Tráº¡ng thÃ¡i táº¡m dá»«ng do há»Ÿ ngÃ³n tay
            // 1. Clear FIFO / Reset DSP CHá»ˆ 1 Láº¦N khi entry Ä‘á»ƒ trÃ¡nh náº¡p dá»¯ liá»‡u rÃ¡c
            // 2. Nháº¯c nhá»Ÿ báº±ng LED & Rung nháº¹ náº¿u trÆ°á»›c Ä‘Ã³ á»Ÿ ACTIVE

            somniguard_led_display(FSM_TOP_OFF_FINGER_SUSPEND);

            // Entry action: Chá»‰ thá»±c hiá»‡n 1 láº§n khi má»›i vÃ o state
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
                }
                fsm->requested_ppg_freq = 0;
                fsm->requested_imu_freq = 0;
                fsm->off_finger_entry_done = true;
            }

            // 2. CÆ  CHáº¾ Äá»ŒC THá»¬ Äá»ŠNH Ká»² (PROBE) Má»–I 1000MS
            static uint32_t last_probe_ms = 0;
            if (timestamp_ms - last_probe_ms >= 1000UL)
            {
                last_probe_ms = timestamp_ms;
                if (fsm->hub != nullptr)
                {
                    // Báº­t MAX30102 trong 50ms Ä‘á»ƒ DataProcessingTask Ä‘á»c thá»­ máº«u má»›i thá»±c táº¿ tá»« pháº§n cá»©ng
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

                    // Ä á» c máº«u cuá»‘i cÃ¹ng trong buffer (Ä‘Ã£ Ä‘Æ°á»£c interrupt task fill)
                    uint32_t ppgIR = 0;
                    // Xáº£ háº¿t trá»« 1 máº«u cuá»‘i Ä‘á»ƒ láº¥y giÃ¡ trá»‹ má»›i nháº¥t
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
            // 3. Xá»¬ LÃ CHUYá»‚N STATE
            if (fsm->dsp_pro.is_finger_attached)
            {
                // ThÃ´ng bÃ¡o ngÃ³n tay Ä‘Ã£ Ä‘eo trá»Ÿ láº¡i qua BLE
                somniguard_ble_notify_event(
                    SOMNIGUARD_BLE_EVT_TYPE_SENSOR_STATUS,
                    SOMNIGUARD_BLE_EVT_CODE_FINGER_ATTACHED,
                    (uint16_t)(timestamp_ms - fsm->top_state_entry_ms),
                    0);
                // KhÃ´i phá»¥c láº¡i tráº¡ng thÃ¡i ACTIVE_MODE hoáº·c prev_state
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
                // QuÃ¡ 60 giÃ¢y khÃ´ng Ä‘eo láº¡i -> Chuyá»ƒn INACTIVE (Cháº¡y EM4 Shutoff)
                fsm->top_state = FSM_TOP_INACTIVE;
                fsm->off_finger_entry_done = false;
            }
            break;
        }

        case FSM_TOP_ACTIVE_MODE:
        {
            /**
             * Mode ACTIVE: Khi ngÆ°á»i dÃ¹ng cÃ²n thá»©c / hoáº¡t Ä‘á»™ng ban ngÃ y
             * - Hiá»ƒn thá»‹ tráº¡ng thÃ¡i LED
             * - Äo IMU 25Hz Ä‘á»ƒ biáº¿t ngÆ°á»i dÃ¹ng Ä‘Ã£ ngá»§ hay cÃ²n váº­n Ä‘á»™ng
             * - Äo PPG 1Hz kiá»ƒm tra Ä‘eo thiáº¿t bá»‹
             * - Äáº¡t Ä‘iá»u kiá»‡n khÃ´ng cá»­ Ä‘á»™ng 3 phÃºt -> chuyá»ƒn NORMAL SLEEP
             */

            somniguard_led_display(FSM_TOP_ACTIVE_MODE);

            // Kiá»ƒm tra tuá»™t/thÃ¡o ngÃ³n tay
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
             * Mode NORMAL SLEEP: Theo dÃµi khi ngÆ°á»i dÃ¹ng ngá»§
             * - Thu tháº­p PPG 50Hz
             * - Thu tháº­p IMU 50Hz
             * - PhÃ¡t hiá»‡n báº¥t thÆ°á»ng (SpO2 sá»¥t hoáº·c AI Apnea) -> chuyá»ƒn DEEP ANALYSIS
             * - PhÃ¡t hiá»‡n thá»©c giáº¥c (cá»±a quáº­y 15s) -> chuyá»ƒn ACTIVE MODE
             * - ThÃ¡o thiáº¿t bá»‹ lÃ¢u -> chuyá»ƒn INACTIVE
             */
            somniguard_led_display(FSM_TOP_NORMAL_SLEEP);

            // Kiá»ƒm tra tuá»™t ngÃ³n tay
            if (!fsm->dsp_pro.is_finger_attached)
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_OFF_FINGER_SUSPEND;
                fsm->top_state_entry_ms = now_ms;
                break;
            }

            // Kiá»ƒm tra ngÆ°á»i dÃ¹ng thá»©c giáº¥c (cá»±a quáº­y liÃªn tá»¥c 15s)
            if (fsm->wake_motion_start_ms > 0 &&
                (timestamp_ms - fsm->wake_motion_start_ms >= FSM_WAKE_MOTION_TIME_MS))
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_ACTIVE_MODE;
                fsm->top_state_entry_ms = now_ms;
            }

            // PhÃ¡t hiá»‡n ngÆ°ng thá»Ÿ/báº¥t thÆ°á»ng -> DEEP ANALYSIS
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
             * Mode DEEP ANALYSIS: PhÃ¢n tÃ­ch AI & Can thiá»‡p Ä‘a cáº¥p Ä‘á»™
             * - Cháº¡y model AI káº¿t há»£p cÃ¹ng káº¿t quáº£ DSP, Motion Ä‘á»ƒ Ä‘Æ°a ra can thiá»‡p
             * - Náº¿u phá»¥c há»“i tá»‘t -> chuyá»ƒn NORMAL SLEEP
             * - Náº¿u khÃ´ng phá»¥c há»“i -> tÄƒng cáº¥p Ä‘á»™ can thiá»‡p (Rung máº¡nh / BLE + CÃ²i)
             * - Khi ngÆ°á»i dÃ¹ng thá»©c dáº­y / thÃ¡o thiáº¿t bá»‹ lÃ¢u -> chuyá»ƒn INACTIVE
             */
            somniguard_led_display(FSM_TOP_DEEP_ANALYSIS);
            // Kiá»ƒm tra tuá»™t ngÃ³n tay
            if (!fsm->dsp_pro.is_finger_attached)
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_OFF_FINGER_SUSPEND;
                fsm->top_state_entry_ms = now_ms;
                break;
            }

            // Khi ngÆ°á»i dÃ¹ng thá»©c dáº­y (cá»±a quáº­y liÃªn tá»¥c 15s) -> chuyá»ƒn ACTIVE MODE
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

            // Phá»¥c há»“i tá»‘t: SpO2 khÃ´i phá»¥c >= 95% vÃ  sub_state vá» IDLE -> NORMAL_SLEEP
            if (fsm->dsp_res.signal_valid && fsm->dsp_res.spo2 >= 95.0f && fsm->sub_state == SUB_INTERVENT_IDLE)
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                fsm->prev_top_state = fsm->top_state;
                fsm->top_state = FSM_TOP_NORMAL_SLEEP;
                fsm->top_state_entry_ms = now_ms;

                // Reset sub-state vá» BUFFERING Ä‘á»ƒ tÃ­ch lÅ©y láº¡i baseline SpO2 sau can thiá»‡p
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
 * @brief Ham RTOS Task Ä‘á»™c láº­p thá»±c thi FSM trong state DEEP ANALYSIS
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

    while (1)
    {
        if (fsm->top_state == FSM_TOP_DEEP_ANALYSIS)
        {
            uint32_t timestamp_ms = pdTICKS_TO_MS(xTaskGetTickCount());
            uint32_t elapsed_in_sub = timestamp_ms - fsm->sub_state_entry_ms;

            switch (fsm->sub_state)
            {
            case SUB_INTERVENT_IDLE:
                // ÄÃ¡nh giÃ¡ má»©c Ä‘á»™ nghi ngá» Ä‘á»ƒ chá»n cáº¥p Ä‘á»™ can thiá»‡p ban Ä‘áº§u
                // Cáº­p nháº­t káº¿t quáº£ cháº©n Ä‘oÃ¡n AI
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
                // Rung nháº¹ cáº¥p 1 (3 giÃ¢y)
                fsm->vibrate_level = 1;
                fsm->buzzer_alarm = false;
                fsm->ble_sos_flag = false;

                if (elapsed_in_sub >= FSM_MILD_VIB_DURATION_MS)
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_EVALUATE_RECOVERY);
                }
                break;

            case SUB_INTERVENT_STRONG_VIBRATE:
                // Rung máº¡nh cáº¥p 2 (5 giÃ¢y)
                fsm->vibrate_level = 2;
                fsm->buzzer_alarm = false;

                if (elapsed_in_sub >= FSM_STRONG_VIB_DURATION_MS)
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_EVALUATE_RECOVERY);
                }
                break;

            case SUB_INTERVENT_BLE_ALARM:
                // Nguy cáº¥p: Rung máº¡nh + CÃ²i bÃ¡o Ä‘á»™ng + PhÃ¡t BLE SOS cá»©u há»™
                fsm->vibrate_level = 2;
                fsm->buzzer_alarm = true;
                fsm->ble_sos_flag = true;

                // Náº¿u ngÆ°á»i dÃ¹ng giáº­t mÃ¬nh cá»±a quáº­y hoáº·c SpO2 há»“i phá»¥c -> chuyá»ƒn sang Ä‘Ã¡nh giÃ¡
                if (fsm->motion_res.is_moving || (fsm->dsp_res.signal_valid && fsm->dsp_res.spo2 >= 90.0f))
                {
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_EVALUATE_RECOVERY);
                }
                break;

            case SUB_INTERVENT_EVALUATE_RECOVERY:
                // Táº¯t rung Ä‘á»ƒ theo dÃµi Ä‘Ã¡p á»©ng sinh lÃ½
                fsm->vibrate_level = 0;
                fsm->buzzer_alarm = false;
                fsm->ble_sos_flag = false;
                // ÄÃ¡nh giÃ¡ chá»‰ sá»‘ phá»¥c há»“i sau can thiá»‡p (10 giÃ¢y)
                if (fsm->dsp_res.signal_valid && fsm->dsp_res.spo2 >= 95.0f)
                {
                    // ÄÃ£ khÃ´i phá»¥c thÃ nh cÃ´ng -> Vá» IDLE (Top-FSM sáº½ chuyá»ƒn sang NORMAL_SLEEP)
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_IDLE);
                }
                else if (elapsed_in_sub >= FSM_EVALUATE_TIMEOUT_MS)
                {
                    // Háº¿t thá»i gian Ä‘Ã¡nh giÃ¡ mÃ  chÆ°a khÃ´i phá»¥c -> TÄƒng cáº¥p Ä‘á»™ can thiá»‡p
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
 *@brief Ham RTOS Task Ä‘á»™c láº­p thá»±c thi FSM trong state ACTIVE MODE
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
                // Entry action: Chá»‰ config sensor 1 láº§n khi má»›i vÃ o state
                if (!fsm->active_init_done)
                {
                    // Táº¯t cÃ¡c thiáº¿t bá»‹ cáº£nh bÃ¡o
                    fsm->vibrate_level = 0;
                    fsm->buzzer_alarm = false;
                    fsm->ble_sos_flag = false;

                    // Äáº·t cáº¥u hÃ¬nh láº¥y máº«u tiáº¿t kiá»‡m pin
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_ACTIVE_HZ; // 25Hz
                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_ACTIVE_HZ; // 1Hz
                    fsm->hub->MAX30102_driver().driver().wakeUp();
                    //   fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    // fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    fsm->active_init_done = true;
                }
                // AGC Ä‘Ã£ cháº¡y trong app_init(), khÃ´ng gá»i á»Ÿ Ä‘Ã¢y ná»¯a
                // Sau 1s calib xong -> Chuyá»ƒn sang WAKEFUL
                if (elapsed_in_sub >= 1000)
                {
                    somniguard_fsm_set_active_state(fsm, SUB_ACTIVE_WAKEFUL);
                }
                break;

            case SUB_ACTIVE_WAKEFUL:
                // Config sensor chá»‰ thá»±c hiá»‡n khi chuyá»ƒn state (trong somniguard_fsm_set_active_state)
                // KhÃ´ng cáº§n gá»i setSampleRate má»—i 100ms vÃ¬ freq khÃ´ng Ä‘á»•i trong state nÃ y

                // Náº¿u ngÆ°á»i dÃ¹ng náº±m yÃªn liÃªn tá»¥c (quiet_duration >= 60s) -> Sang PRE_SLEEP
                if (fsm->quiet_duration_ms >= 60000UL)
                {
                    // NÃ¢ng freq khi chuyá»ƒn sang PRE_SLEEP
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_SLEEP_HZ; // 50Hz
                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_SLEEP_HZ; // 50Hz
                                                                          //  fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    // fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    somniguard_fsm_set_active_state(fsm, SUB_ACTIVE_PRE_SLEEP);
                }
                break;

            case SUB_ACTIVE_PRE_SLEEP:
                // Freq Ä‘Ã£ Ä‘Æ°á»£c set khi entry tá»« WAKEFUL, khÃ´ng cáº§n gá»i láº¡i má»—i 100ms

                // Náº¿u cá»±a quáº­y máº¡nh trá»Ÿ láº¡i -> Quay vá» WAKEFUL
                if (fsm->motion_res.is_moving)
                {
                    // Háº¡ freq vá» ACTIVE khi quay láº¡i WAKEFUL
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_ACTIVE_HZ; // 25Hz
                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_ACTIVE_HZ; // 1Hz
                                                                           // fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    // fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    somniguard_fsm_set_active_state(fsm, SUB_ACTIVE_WAKEFUL);
                }
                // Náº¿u náº±m yÃªn Ä‘á»§ 3 phÃºt (FSM_SLEEP_ENTER_TIME_MS) vÃ  tÃ­n hiá»‡u tá»‘t -> Chuyá»ƒn NORMAL_SLEEP
                else if (fsm->quiet_duration_ms >= FSM_SLEEP_ENTER_TIME_MS && fsm->dsp_res.signal_valid)
                {
                    fsm->sleep_buffering_entry_done = false; // Reset cho sleep entry
                    somniguard_fsm_set_top_state(fsm, FSM_TOP_NORMAL_SLEEP);
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_IDLE);
                }
                break;

            case SUB_ACTIVE_SPOT_CHECK:
                // Config chá»‰ 1 láº§n khi entry (freq Ä‘Ã£ set bá»Ÿi caller)
                if (elapsed_in_sub >= 30000UL)
                { // Sau 30s Ä‘o xong
                    // Háº¡ freq vá» ACTIVE khi quay láº¡i WAKEFUL
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_ACTIVE_HZ;
                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_ACTIVE_HZ;
                    //  fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    // fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
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
 * @brief Ham RTOS Task Ä‘á»™c láº­p thá»±c thi FSM trong state NORMAL SLEEP
 *
 * Nhiá»‡m vá»¥:
 *   1. SUB_SLEEP_BUFFERING  â€” Chá» tensor buffer Ä‘áº§y 40s, sau Ä‘Ã³ chuyá»ƒn MONITORING.
 *   2. SUB_SLEEP_MONITORING â€” Theo dÃµi liÃªn tá»¥c 2 ngÆ°á»¡ng báº¥t thÆ°á»ng:
 *        NgÆ°á»¡ng 1 (cá»©ng): SpO2 < FSM_SPO2_CRITICAL_THRESHOLD (93%) â†’ DEEP_ANALYSIS ngay
 *        NgÆ°á»¡ng 2 (má»m) : SpO2 giáº£m >= APNEA_DROP_THRESHOLD (4%) so vá»›i baseline
 *                          vÃ  kÃ©o dÃ i >= FSM_ANOMALY_SUSTAIN_MS (10s) â†’ DEEP_ANALYSIS
 *
 * LÆ°u Ã½: AI KHÃ”NG Ä‘Æ°á»£c gá»i á»Ÿ Ä‘Ã¢y. Viá»‡c gá»i AI chá»‰ xáº£y ra trong DEEP_ANALYSIS.
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
            // SUB_SLEEP_BUFFERING: TÃ­ch lÅ©y buffer Tensor 40s
            // Chá» DataProcessingTask náº¡p Ä‘á»§ 1000 máº«u vÃ o buffer_pro trÆ°á»›c khi
            // chuyá»ƒn sang MONITORING. KhÃ´ng can thiá»‡p vÃ o luá»“ng data.
            // =================================================================
            case SUB_SLEEP_BUFFERING:
                // Entry action: Config sensor 1 láº§n
                if (!fsm->sleep_buffering_entry_done && fsm->prev_top_state != FSM_TOP_DEEP_ANALYSIS)
                {
                    fsm->vibrate_level = 0;
                    fsm->buzzer_alarm = false;
                    fsm->ble_sos_flag = false;

                    fsm->requested_ppg_freq = PPG_SAMPLING_RATE_SLEEP_HZ;
                    fsm->requested_imu_freq = IMU_SAMPLING_RATE_SLEEP_HZ;

                    //   fsm->hub->MAX30102_driver().setSampleRate(fsm->requested_ppg_freq);
                    //    fsm->hub->imu_driver().setup(fsm->requested_imu_freq, 1);
                    fsm->sleep_buffering_entry_done = true;

                    printf("[SLEEP] Buffering... waiting for %d samples.\r\n", TENSOR_MAX_ROWS);
                }

                // Chá» buffer Ä‘áº§y Ä‘á»§ 40s (1000 máº«u) má»›i chuyá»ƒn sang MONITORING
                if (fsm->buffer_pro.is_full)
                {
                    // Chá»¥p baseline SpO2 táº¡i thá»i Ä‘iá»ƒm báº¯t Ä‘áº§u MONITORING
                    fsm->spo2_baseline = fsm->dsp_res.signal_valid ? fsm->dsp_res.spo2 : 98.0f;
                    fsm->anomaly_detect_ms = 0;
                    fsm->anomaly_sustained = false;

                    // printf("[SLEEP] Buffer full (%d samples). Starting monitoring. SpO2 baseline: %d%%\r\n",
                    //        TENSOR_MAX_ROWS, (int)fsm->spo2_baseline);

                    somniguard_fsm_set_normal_state(fsm, SUB_SLEEP_MONITORING);
                }
                break;

            // =================================================================
            // SUB_SLEEP_MONITORING: Theo dÃµi báº¥t thÆ°á»ng liÃªn tá»¥c
            // =================================================================
            case SUB_SLEEP_MONITORING:
            {
                // Bá» qua náº¿u tÃ­n hiá»‡u PPG chÆ°a á»•n Ä‘á»‹nh
                if (!fsm->dsp_res.signal_valid)
                    break;

                float spo2 = fsm->dsp_res.spo2;

                // --- NgÆ°á»¡ng 1 (Cá»©ng): SpO2 tá»¥t dÆ°á»›i 93% ---
                // Nguy cÆ¡ ngÆ°ng thá»Ÿ rÃµ rÃ ng â†’ chuyá»ƒn DEEP_ANALYSIS ngay láº­p tá»©c
                if (spo2 < FSM_SPO2_WARN_THRESHOLD)
                {
                    // printf("[SLEEP] ANOMALY Tier-1: SpO2 %.1f%% < %.0f%% threshold! -> DEEP_ANALYSIS\r\n",
                    //        spo2, FSM_SPO2_CRITICAL_THRESHOLD);

                    fsm->anomaly_detect_ms = 0;
                    fsm->anomaly_sustained = false;
                    somniguard_fsm_set_top_state(fsm, FSM_TOP_DEEP_ANALYSIS);
                    break;
                }
                if (spo2 < FSM_SPO2_CRITICAL_THRESHOLD && fsm->dsp_res.signal_valid && fsm->motion_pro.motion_threshold < PARAM_IMU_MOTION_THRESHOLD)
                {
                    somniguard_fsm_set_top_state(fsm, FSM_TOP_DEEP_ANALYSIS);
                    somniguard_fsm_set_sub_state(fsm, SUB_INTERVENT_STRONG_VIBRATE);
                }
                // --- NgÆ°á»¡ng 2 (Má» m): SpO2 drop >= 4% so vá»›i baseline, kÃ©o dÃ i >= 10s ---
                // PhÃ¡t hiá»‡n xu hÆ°á»›ng giáº£m oxy mÃ¡u cháº­m (hypopnea / mild apnea)
                float spo2_drop = fsm->spo2_baseline - spo2;
                if (spo2_drop >= APNEA_DROP_THRESHOLD)
                {
                    if (fsm->anomaly_detect_ms == 0)
                    {
                        // Báº¯t Ä‘á» u Ä‘áº¿m thá» i gian báº¥t thÆ°á» ng bá» n vá»¯ng
                        fsm->anomaly_detect_ms = now_ms;
                        fsm->anomaly_sustained = true;
                        // printf("[SLEEP] ANOMALY Tier-2: SpO2 drop %.1f%% (baseline %.1f%% -> now %.1f%%). Counting...\r\n",
                        //        spo2_drop, fsm->spo2_baseline, spo2);
                    }
                    else if ((now_ms - fsm->anomaly_detect_ms) >= FSM_ANOMALY_SUSTAIN_MS)
                    {
                        // Drop Ä‘Ã£ kÃ©o dÃ i Ä‘á»§ 10s â†’ chuyá»ƒn DEEP_ANALYSIS
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
                    // SpO2 trá»Ÿ vá» bÃ¬nh thÆ°á»ng â†’ reset bá»™ Ä‘áº¿m báº¥t thÆ°á»ng
                    if (fsm->anomaly_sustained)
                    {
                        // printf("[SLEEP] Anomaly cleared. SpO2 recovered to %.1f%%\r\n", spo2);
                    }
                    fsm->anomaly_detect_ms = 0;
                    fsm->anomaly_sustained = false;

                    // Cáº­p nháº­t baseline theo chiá»u tÄƒng (slow-tracking upward only)
                    // Cho phÃ©p baseline pháº£n Ã¡nh SpO2 tá»‘t hÆ¡n náº¿u bá»‡nh nhÃ¢n há»“i phá»¥c
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
    // 1. Äiá»u khiá»ƒn Motor Rung (Haptic)
    switch (fsm->vibrate_level)
    {
    case 0:
        somniguard_haptic_motor(0, 0);
        break; // Táº¯t rung
    case 1:
        somniguard_haptic_motor(HAPTIC_PWM_MILD, HAPTIC_DURATION_MILD_MS);
        break; // Rung nháº¹ (VÃ­ dá»¥ 100/255 trong 3s)
    case 2:
        somniguard_haptic_motor(HAPTIC_PWM_STRONG, HAPTIC_DURATION_STRONG_MS);
        break; // Rung máº¡nh (255/255 trong 5s)
    }

    // 3. Äiá»u khiá»ƒn BLE SOS
    if (fsm->ble_sos_flag)
    {
        somniguard_BLE_control(); // PhÃ¡t gÃ³i tin BLE SOS kháº©n cáº¥p
    }
}

#include "em_emu.h"
#include "em_gpio.h"

void somniguard_enter_em4_shutoff(somniguard_fsm_t *fsm)
{
    //   printf("\r\n[EMU POWER] Entering EM4 Shutoff Mode via EMLIB...\r\n");

    // PhÃ¡t gÃ³i tin BLE bÃ¡o chuáº©n bá»‹ táº¯t nguá»“n
    somniguard_ble_notify_event(
        SOMNIGUARD_BLE_EVT_TYPE_POWER_SYSTEM,
        SOMNIGUARD_BLE_EVT_CODE_EM4_SHUTOFF,
        0, 0);

    // 1. Táº¯t cÃ¡c thiáº¿t bá»‹ ngoáº¡i vi & cáº£m biáº¿n (MAX30102)
    if (fsm != NULL)
    {
        somniguard_power_off(fsm);
        if (fsm->hub != NULL)
        {
            // fsm->hub->MAX30102_driver().shutDown();
        }
    }

    // 2. Cáº¥u hÃ¬nh chÃ¢n nÃºt báº¥m (VD: ChÃ¢n Pin 4) lÃ m ngáº¯t EM4 Wakeup Pin
    // Khi nháº¥n nÃºt, MCU sáº½ tá»± Ä‘á»™ng tá»‰nh dáº­y tá»« EM4 vÃ  Reset thiáº¿t bá»‹
    GPIO_EM4WUExtIntConfig(gpioPortB, 3, 4, false, true);

    // 3. Cáº¥u hÃ¬nh thÃ´ng sá»‘ EM4 (Táº¯t Unretained RAM Ä‘á»ƒ tiáº¿t kiá»‡m pin tá»‘i Ä‘a ~100nA)
    EMU_EM4Init_TypeDef em4Init = EMU_EM4INIT_DEFAULT;
    em4Init.retainLfxo = false;
    em4Init.em4State = emuEM4Shutoff; // Má»©c Shutoff tiáº¿t kiá»‡m pin nháº¥t
    EMU_EM4Init(&em4Init);

    // 4. Lá»‡nh Ã©p MCU nháº£y tháº³ng vÃ o EM4 Shutoff
    EMU_EnterEM4();
}
