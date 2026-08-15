

/////////////////////////////////////////////////////////////////////////////////////
// /***************************************************************************//**
//  * @file
//  * @brief Top level application functions
//  *******************************************************************************
//  * # License
//  * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
//  *******************************************************************************
//  *
//  * The licensor of this software is Silicon Laboratories Inc. Your use of this
//  * software is governed by the terms of Silicon Labs Master Software License
//  * Agreement (MSLA) available at
//  * www.silabs.com/about-us/legal/master-software-license-agreement. This
//  * software is distributed to you in Source Code format and is governed by the
//  * sections of the MSLA applicable to Source Code.
//  *
//  ******************************************************************************/

// /***************************************************************************//**
//  * Initialize application.
//  ******************************************************************************/

#include "sl_iostream.h"
#include "stdio.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "MAX30102_driver/MAX30105.h"
#include "em_gpio.h"
#include "em_i2c.h"
#include "MAX30102_manager.h"
#include "sensor_hub/sensor_hub.h"
#include "somniguard_layer/somniguard_motion.h"
#include "somniguard_layer/somniguard_dsp.h"
#include "somniguard_layer/somniguard_buffer.h"
#include "somniguard_layer/somniguard_fsm.h"
#include "other_driver/ble_notification_manager.h"
#include <stdlib.h>
#include <string.h>
#include "model/model.h"

static SensorHub mySensorHub;
static somniguard_fsm_t myFSM;

#define USE_MOCK_TENSOR_BUFFER 1

#if USE_MOCK_TENSOR_BUFFER
// Hàm sinh dữ liệu Tensor Buffer giả lập:
// 1. Giai đoạn Khởi động (15s đầu): Giả lập chuyển từ ACTIVE -> NORMAL_SLEEP
// 2. Các chu kỳ tiếp theo: 45s NORMAL -> 15s DEEP ANALYSIS (Chạy AI Model)
static void get_mock_tensor_metrics(somniguard_fsm_t *fsm, uint32_t timestamp_ms, float *out_spo2, float *out_bpm, float *out_motion)
{
    // Giai đoạn 1: 15 giây đầu khởi động (chuyển từ ACTIVE_MODE -> NORMAL_SLEEP)
    if (timestamp_ms < 15000)
    {
        *out_spo2 = 97.5f;
        *out_bpm = 72.0f;
        *out_motion = 0.001f; // Nằm yên tuyệt đối

        // Đẩy nhanh quiet_duration của FSM để chuyển ACTIVE -> NORMAL nhanh chóng sau 5-15s
        if (fsm != NULL && fsm->top_state == FSM_TOP_ACTIVE_MODE)
        {
            if (timestamp_ms >= 5000)
            {
                fsm->quiet_duration_ms += 20000UL; // Vượt ngưỡng FSM_SLEEP_ENTER_TIME_MS (120,000ms)
            }
        }
        return;
    }

    // Giai đoạn 2: Chu kỳ lặp 60 giây (45s Normal + 15s Deep Analysis)
    uint32_t cycle_ms = (timestamp_ms - 15000) % 60000;

    if (cycle_ms < 45000)
    {
        // 45 giây: Trạng thái NORMAL (Giấc ngủ bình thường)
        *out_spo2 = 97.5f + ((float)(timestamp_ms % 1000) / 2000.0f); // 97.5% - 98.0%
        *out_bpm = 70.0f + ((float)(timestamp_ms % 2000) / 1000.0f);  // 70 - 72 BPM
        *out_motion = 0.002f;                                         // Nằm yên
    }
    else
    {
        // 15 giây: Trạng thái DEEP ANALYSIS (Phân tích ngưng thở bằng AI Model)
        // SpO2 giảm nhanh từ 97.5% xuống 82.0% trong 15 giây để kích hoạt AI Inference
        float elapsed_sec = (float)(cycle_ms - 45000) / 1000.0f;
        float spo2_drop = (elapsed_sec / 15.0f) * 15.5f; // Drop 15.5% trong 15s
        *out_spo2 = 97.5f - spo2_drop;
        *out_bpm = 72.0f + (elapsed_sec * 0.8f); // Nhịp tim tăng
        *out_motion = 0.008f;
    }
}
#endif

void DataProcessingTask(void *pvParameters)
{
    somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
    sensor_hub_data_t data;
    float ac_ir_buf[FEATURE_RATE_IR_AC_HZ];
    uint16_t idx = 0;

    while (1)
    {
        // 1. Rút data thô từ SensorHub
        while (fsm->hub->getPPGdata(&data))
        {
            uint32_t timestamp_ms = pdTICKS_TO_MS(xTaskGetTickCount());
            ac_ir_buf[idx] = (float)data.ppg_ir;
            idx = (idx + 1) % FEATURE_RATE_IR_AC_HZ;

            // 2. Chạy thuật toán DSP tính SpO2 & BPM
            bool has_new_stride = somniguard_dsp_process_sample(
                &fsm->dsp_pro,
                data.ppg_red,
                data.ppg_ir,
                timestamp_ms,
                &fsm->dsp_res);

            somniguard_raw_imu_t rawIMU;
            rawIMU.ax = data.ax;
            rawIMU.ay = data.ay;
            rawIMU.az = data.az;
            rawIMU.gx = data.gx;
            rawIMU.gy = data.gy;
            rawIMU.gz = data.gz;

            // 3. Chạy thuật toán Motion tính độ lệch chuẩn cựa tay
            somniguard_motion_process_sample(
                &fsm->motion_pro,
                &rawIMU,
                &fsm->motion_res);

            // 4. Khi có stride DSP mới (mỗi 1s/0.5s), đẩy đầy đủ 4 kênh vào Tensor Buffer
            if (has_new_stride)
            {
                float push_spo2 = fsm->dsp_res.spo2;
                float push_bpm = fsm->dsp_res.heart_rate;
                float push_motion = fsm->motion_res.motion_energy;

#if USE_MOCK_TENSOR_BUFFER
                get_mock_tensor_metrics(fsm, timestamp_ms, &push_spo2, &push_bpm, &push_motion);
                fsm->dsp_res.spo2 = push_spo2;
                fsm->dsp_res.heart_rate = push_bpm;
                fsm->motion_res.motion_energy = push_motion;
                fsm->dsp_res.signal_valid = true;
#endif

                somniguard_buffer_push_50hz(
                    &fsm->buffer_pro,
                    push_spo2,
                    push_bpm,
                    ac_ir_buf,
                    push_motion);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // Chu kỳ 20ms = 50Hz
    }
}

// =========================================================================
// Cấu hình Chân RGB LED Báo Trạng Thái Hệ Thống (xG26 DevKit BRD2608A / DK2608A)
// - Trên kit BRD2608A, LED RGB (U12) được nối với các chân GPIO:
//   + RGB RED:   gpioPortA, pin 4 (hoặc gpioPortB, pin 4)
//   + RGB GREEN: gpioPortB, pin 0
//   + RGB BLUE:  gpioPortB, pin 2
// =========================================================================
#define SYSTEM_STATUS_LED_PIN 4 // Mặc định dùng kênh RED/BLUE trên BRD2608A
#define SYSTEM_STATUS_LED_PORT gpioPortB
// Task nhấp nháy LED báo trạng thái thiết bị đang hoạt động (Toggle 500ms ON / 500ms OFF để tương thích 100% Active-Low & Active-High)
// Đặt % Cường độ sáng mong muốn (Từ 1% đến 100%)
// Mặc định 10% -> Tiết kiệm pin tối đa và dịu mắt khi đeo ngủ ban đêm

#define SYSTEM_STATUS_LED_BRIGHTNESS_PERCENT 10
// Task nhấp nháy TẤT CẢ các chân LED / RGB LED trên kit BRD2608A Rev A04
// - PA04: RGB Red | PB00: RGB Green | PB02: RGB Blue | PB04: LED0 | PB05: LED1
void LedBlinkyTask(void *pvParameters)
{
    (void)pvParameters;

    // Cấu hình tất cả các chân LED làm Output Push-Pull
    GPIO_PinModeSet(gpioPortA, 4, gpioModePushPull, 1);
    GPIO_PinModeSet(gpioPortB, 0, gpioModePushPull, 1);
    GPIO_PinModeSet(gpioPortB, 2, gpioModePushPull, 1);

    while (1)
    {
        // 1. Kéo xuống LOW (Mạch Active-Low trên BRD2608A: Pull 0 = SÁNG TẤT CẢ LED)
        GPIO_PinOutClear(gpioPortA, 4);
        GPIO_PinOutClear(gpioPortB, 0);
        GPIO_PinOutClear(gpioPortB, 2);

        vTaskDelay(pdMS_TO_TICKS(500));

        // 2. Kéo lên HIGH (Pull 1 = TẮT TẤT CẢ LED)
        GPIO_PinOutSet(gpioPortA, 4);
        GPIO_PinOutSet(gpioPortB, 0);
        GPIO_PinOutSet(gpioPortB, 2);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Task log trạng thái và các thông số vận hành FSM
void FsmLoggerTask(void *pvParameters)
{
    somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
    printf("--- SomniGuard FSM Logger Task Started ---\r\n");
    fflush(stdout);

    uint32_t log_counter = 0;
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000)); // In log mỗi 1 giây
        log_counter++;

        const char *top_str = somniguard_top_state_str(fsm->top_state);
        const char *sub_str = "N/A";
        switch (fsm->top_state)
        {
        case FSM_TOP_ACTIVE_MODE:
            sub_str = somniguard_active_state_str(fsm->active_state);
            break;
        case FSM_TOP_NORMAL_SLEEP:
            sub_str = somniguard_normal_state_str(fsm->normal_state);
            break;
        case FSM_TOP_DEEP_ANALYSIS:
            sub_str = somniguard_sub_state_str(fsm->sub_state);
            break;
        default:
            sub_str = "NONE";
            break;
        }

        int spo2_i = (int)fsm->dsp_res.spo2;
        int spo2_d = (int)(fabsf(fsm->dsp_res.spo2 - (float)spo2_i) * 10.0f);
        int bpm_i = (int)fsm->dsp_res.heart_rate;
        int motion_i = (int)(fsm->motion_res.motion_energy * 1000.0f); // mg

        printf("[FSM LOG #%lu] TopState: %-18s | SubState: %-18s | Finger: %-3s | SpO2: %2d.%01d%% | BPM: %3d | Motion: %4d mg (%s) | Quiet: %lu ms | Vib: %u | Buz: %s | BLE: %s | Buffer: %u/%d\r\n",
               (unsigned long)log_counter,
               top_str,
               sub_str,
               fsm->dsp_pro.is_finger_attached ? "YES" : "NO",
               spo2_i, spo2_d,
               bpm_i,
               motion_i,
               fsm->motion_res.is_moving ? "MOVE" : "REST",
               (unsigned long)fsm->quiet_duration_ms,
               fsm->vibrate_level,
               fsm->buzzer_alarm ? "ON" : "OFF",
               somniguard_ble_is_subscribed() ? "ON" : "OFF",
               fsm->buffer_pro.count,
               TENSOR_MAX_ROWS);
        fflush(stdout);
    }
}

// Task gửi dữ liệu Telemetry 12 bytes định kỳ (1Hz)
void BleTelemetryTask(void *pvParameters)
{
    somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
    uint16_t seq_num = 0;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Gửi telemetry mỗi 1 giây liên tục

        somniguard_ble_telemetry_pkt_t telem;
        telem.seq_num = ++seq_num;
        telem.spo2_x100 = (uint16_t)(fsm->dsp_res.spo2 * 100.0f);
        telem.hr_x10 = (uint16_t)(fsm->dsp_res.heart_rate * 10.0f);
        telem.motion_mg = (uint16_t)(fsm->motion_res.motion_energy * 1000.0f);

        uint8_t posture = 0; // 0: Supine
        uint8_t finger_flag = fsm->dsp_pro.is_finger_attached ? (1 << 3) : 0;
        uint8_t valid_flag = fsm->dsp_res.signal_valid ? (1 << 4) : 0;
        telem.posture_flags = (posture & 0x07) | finger_flag | valid_flag;

        telem.top_fsm_state = (uint8_t)fsm->top_state;
        telem.sub_fsm_state = (uint8_t)fsm->normal_state;
        telem.battery_level = 100;

        somniguard_ble_notify_telemetry(&telem);
    }
}

static void main_app_task(void *pvParameters)
{
    (void)pvParameters;
    char line_buf[512];
    int idx = 0;

    while (1)
    {
        // Đọc 1 ký tự từ Serial
        int c = getchar();
        if (c == EOF)
        {
            vTaskDelay(pdMS_TO_TICKS(1)); // Dùng vTaskDelay thay vì taskYIELD để tránh chiếm dụng 100% CPU
            continue;
        }

        if (c == '\n' || c == '\r')
        {
            if (idx > 0)
            {
                line_buf[idx] = '\0';

                if (strncmp(line_buf, "RESET", 5) == 0)
                {
                    reset_buffer();
                    fflush(stdout);
                }
                else if (line_buf[0] == 'W' && line_buf[1] == ',')
                {
                    float frame[28];
                    int count = 0;
                    char *p = line_buf + 2;
                    while (p && *p && count < 28)
                    {
                        char *next_comma = strchr(p, ',');
                        if (next_comma)
                            *next_comma = '\0';
                        frame[count++] = atof(p);
                        if (next_comma)
                            p = next_comma + 1;
                        else
                            break;
                    }
                    if (count == 28)
                    {
                        process_new_frame(frame);
                        fflush(stdout);
                    }
                    else
                    {
                        printf("ERR:BadFrame:%d\r\n", count);
                        fflush(stdout);
                    }
                }
                idx = 0; // Reset buffer
            }
        }
        else
        {
            if (idx < sizeof(line_buf) - 1)
            {
                line_buf[idx++] = c;
            }
        }
    }
}

void app_init(void)
{
    printf("========== APP INIT FSM RUN START ==========\r\n");

    // // Khởi tạo BLE Notification Manager
    // somniguard_ble_manager_init();

    // // Khởi tạo AI Model
    // init_model();

    // // Khởi tạo Sensor Hub (Cấu hình IMU & MAX30102 ở 50Hz)
    // if (!mySensorHub.initSensors(50))
    // {
    //     printf("WARNING: Failed to initialize SensorHub! Continuing system boot...\r\n");
    // }
    // else
    // {
    //     printf("SensorHub initialized successfully.\r\n");
    // }

    // Chạy AGC calibration trước khi tạo FSM tasks
    // mySensorHub.agcAmplitudeLed();

    // // Khởi tạo Bộ Não FSM
    // somniguard_fsm_init(&myFSM, &mySensorHub);

    // // 1. Task Thu thập & Xử lý Dữ liệu Cảm biến
    // xTaskCreate(
    //     DataProcessingTask,
    //     "DataProc",
    //     512,
    //     &myFSM,
    //     tskIDLE_PRIORITY + 3,
    //     NULL);

    // // 2. Task Bộ Não FSM Chính (Top-Level FSM Runner - 100ms)
    // xTaskCreate(
    //     somniguard_fsm_task,
    //     "FsmMain",
    //     512,
    //     &myFSM,
    //     tskIDLE_PRIORITY + 2,
    //     NULL);

    // // 3. Sub-FSM Task cho Active Mode
    // xTaskCreate(
    //     somniguard_active_mode_task,
    //     "FsmActive",
    //     384,
    //     &myFSM,
    //     tskIDLE_PRIORITY + 1,
    //     NULL);

    // // 4. Sub-FSM Task cho Normal Sleep
    // xTaskCreate(
    //     somniguard_normal_sleep_task,
    //     "FsmSleep",
    //     384,
    //     &myFSM,
    //     tskIDLE_PRIORITY + 1,
    //     NULL);

    // // 5. Sub-FSM Task cho Deep Analysis / Can thiệp
    // xTaskCreate(
    //     somniguard_deep_analysis_task,
    //     "FsmDeep",
    //     384,
    //     &myFSM,
    //     tskIDLE_PRIORITY + 1,
    //     NULL);

    // 6. Task Log Trạng Thái FSM & Thông Số Sinh Lý (Commented for low power profiling)
    // xTaskCreate(
    //     FsmLoggerTask,
    //     "FsmLogger",
    //     512,
    //     &myFSM,
    //     tskIDLE_PRIORITY + 1,
    //     NULL);

    // // 7. Task BLE Telemetry Publishing (1Hz / 0.2Hz)
    // xTaskCreate(
    //     BleTelemetryTask,
    //     "BleTelem",
    //     384,
    //     &myFSM,
    //     tskIDLE_PRIORITY + 1,
    //     NULL);

    // // 8. Task Serial AI Test
    // xTaskCreate(
    //     main_app_task,
    //     "SerialTask",
    //     2048,
    //     NULL,
    //     tskIDLE_PRIORITY + 1,
    //     NULL);
    xTaskCreate(
        LedBlinkyTask,
        "LedBlinky",
        512,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL);

    printf("========== APP INIT FSM RUN DONE ==========\r\n");
}

void app_process_action(void)
{
}
