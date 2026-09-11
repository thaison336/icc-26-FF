

/////////////////////////////////////////////////////////////////////////////////////
// /***************************************************************************//**
//  * @file
//  * @brief Top level application functions
//  *******************************************************************************
//  * # License
//  * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
//  *******************************************************************************
//  *
//  * The licensor of this software is Silicon Laboratories Inc. Your use of
//  this
//  * software is governed by the terms of Silicon Labs Master Software License
//  * Agreement (MSLA) available at
//  * www.silabs.com/about-us/legal/master-software-license-agreement. This
//  * software is distributed to you in Source Code format and is governed by
//  the
//  * sections of the MSLA applicable to Source Code.
//  *
//  ******************************************************************************/

// /***************************************************************************//**
//  * Initialize application.
//  ******************************************************************************/

#include "app.h"
#include "FreeRTOS.h"
#include "MAX30102_manager.h"
#include "MPU6050_driver/MPU6050.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_i2c.h"
#include "em_timer.h"
#include "model/model.h"
#include "other_driver/actuators_bsp.h"
#include "other_driver/battery_monitor.h"
#include "other_driver/ble_notification_manager.h"
#include "sensor_hub/sensor_hub.h"
#include "sl_iostream.h"
#include "somniguard_layer/somniguard_buffer.h"
#include "somniguard_layer/somniguard_dsp.h"
#include "somniguard_layer/somniguard_fsm.h"
#include "somniguard_layer/somniguard_motion.h"
#include "stdio.h"
#include "task.h"
#include <stdlib.h>
#include <string.h>

static SensorHub mySensorHub;
static somniguard_fsm_t myFSM;

#define USE_MOCK_TENSOR_BUFFER 0

#if USE_MOCK_TENSOR_BUFFER
// Hàm sinh dữ liệu Tensor Buffer giả lập:
// 1. Giai đoạn Khởi động (15s đầu): Giả lập chuyển từ ACTIVE -> NORMAL_SLEEP
// 2. Các chu kỳ tiếp theo: 45s NORMAL -> 15s DEEP ANALYSIS (Chạy AI Model)
static void get_mock_tensor_metrics(somniguard_fsm_t *fsm,
                                    uint32_t timestamp_ms, float *out_spo2,
                                    float *out_bpm, float *out_motion)
{
  // Giai đoạn 1: 15 giây đầu khởi động (chuyển từ ACTIVE_MODE -> NORMAL_SLEEP)
  if (timestamp_ms < 15000)
  {
    *out_spo2 = 97.5f;
    *out_bpm = 72.0f;
    *out_motion = 0.001f; // Nằm yên tuyệt đối

    // Đẩy nhanh quiet_duration của FSM để chuyển ACTIVE -> NORMAL nhanh chóng
    // sau 5-15s
    if (fsm != NULL && fsm->top_state == FSM_TOP_ACTIVE_MODE)
    {
      if (timestamp_ms >= 5000)
      {
        fsm->quiet_duration_ms +=
            20000UL; // Vượt ngưỡng FSM_SLEEP_ENTER_TIME_MS (120,000ms)
      }
    }
    return;
  }

  // Giai đoạn 2: Chu kỳ lặp 600 giây (500s Normal + 100s Deep Analysis)
  uint32_t cycle_ms = (timestamp_ms - 20000) % 600000;

  if (cycle_ms < 550000)
  {
    // 45 giây: Trạng thái NORMAL (Giấc ngủ bình thường)
    *out_spo2 =
        97.5f + ((float)(timestamp_ms % 1000) / 2000.0f);        // 97.5% - 98.0%
    *out_bpm = 70.0f + ((float)(timestamp_ms % 2000) / 1000.0f); // 70 - 72 BPM
    *out_motion = 0.002f;                                        // Nằm yên
  }
  else
  {
    // 15 giây: Trạng thái DEEP ANALYSIS (Phân tích ngưng thở bằng AI Model)
    // SpO2 giảm nhanh từ 97.5% xuống 82.0% trong 15 giây để kích hoạt AI
    // Inference
    float elapsed_sec = (float)(cycle_ms - 500000) / 1000.0f;
    float spo2_drop = (elapsed_sec / 100.0f) * 15.5f; // Drop 15.5% trong 100s
    *out_spo2 = 97.5f - spo2_drop;
    *out_bpm = 72.0f + (elapsed_sec * 0.8f); // Nhịp tim tăng
    *out_motion = 0.008f;
  }
}
#endif

void DataProcessingTask(void *pvParameters)
{
  somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
  printf("--- DataProcessing Task Started ---\r\n");
  sensor_hub_data_t data;
  float ac_ir_buf[FEATURE_RATE_IR_AC_HZ];
  uint16_t idx = 0;

  static uint32_t sample_timestamp_ms = 0;

  while (1)
  {
    // 1. Rút data thô đồng bộ (PPG + IMU) từ SensorHub (kích hoạt bởi ngắt
    // MAX30102)
    while (fsm->is_calibrating)
    {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    while (fsm->hub->getsensordata(&data))
    {
      uint32_t current_tick_ms = pdTICKS_TO_MS(xTaskGetTickCount());
      if (sample_timestamp_ms == 0 ||
          (current_tick_ms > sample_timestamp_ms + 1000))
      {
        sample_timestamp_ms = current_tick_ms;
      }
      else
      {
        sample_timestamp_ms +=
            20; // 50Hz: tăng đều 20ms cho từng mẫu trong batch
      }

      // 2. Chạy thuật toán DSP tính SpO2 & BPM
      bool has_new_stride = somniguard_dsp_process_sample(
          &fsm->dsp_pro, data.ppg_red, data.ppg_ir, sample_timestamp_ms,
          &fsm->dsp_res);
      float dc_ir = fsm->dsp_pro.dc_track_ir;
      float ac_ir_norm =
          (dc_ir > 0.0f) ? (fsm->dsp_pro.lpf_ir_prev / dc_ir) : 0.0f;
      ac_ir_buf[idx] = ac_ir_norm;
      idx = (idx + 1) % FEATURE_RATE_IR_AC_HZ;

      somniguard_raw_imu_t rawIMU;
      rawIMU.ax = data.ax;
      rawIMU.ay = data.ay;
      rawIMU.az = data.az;
      rawIMU.gx = data.gx;
      rawIMU.gy = data.gy;
      rawIMU.gz = data.gz;

      // printf("PPG[R:%lu, IR:%lu] | ACC[%.2f, %.2f, %.2f]g | GYR[%.1f, %.1f,
      // %.1f]dps\r\n",
      //        data.ppg_red, data.ppg_ir,
      //        data.ax, data.ay, data.az,
      //        data.gx, data.gy, data.gz);

      // 3. Chạy thuật toán Motion tính độ lệch chuẩn cựa tay
      somniguard_motion_process_sample(&fsm->motion_pro, &rawIMU,
                                       &fsm->motion_res);

      // 4. Khi có stride DSP mới (mỗi 1s/0.5s), đẩy đầy đủ 4 kênh vào Tensor
      // Buffer
      // if (has_new_stride && (fsm->top_state == FSM_TOP_NORMAL_SLEEP ||
      //                        fsm->top_state == FSM_TOP_DEEP_ANALYSIS))
      if (has_new_stride)
      {
        float push_spo2 = fsm->dsp_res.spo2;
        float push_bpm = fsm->dsp_res.heart_rate;
        float push_motion = fsm->motion_res.motion_energy;
#if USE_MOCK_TENSOR_BUFFER
        get_mock_tensor_metrics(fsm, sample_timestamp_ms, &push_spo2, &push_bpm,
                                &push_motion);
        fsm->dsp_res.spo2 = push_spo2;
        fsm->dsp_res.heart_rate = push_bpm;
        fsm->motion_res.motion_energy = push_motion;
        fsm->dsp_res.signal_valid = true;
#endif
        somniguard_buffer_push_50hz(&fsm->buffer_pro, push_spo2, push_bpm,
                                    ac_ir_buf, push_motion);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20)); // Chu kỳ 20ms = 50Hz
  }
}

// =========================================================================
// Cấu hình Chân RGB LED Báo Trạng Thái Hệ Thống (xG26 DevKit BRD2608A /
// DK2608A)
// - Trên kit BRD2608A, LED RGB (U12) được nối với các chân GPIO:
//   + RGB RED:   gpioPortA, pin 4 (hoặc gpioPortB, pin 4)
//   + RGB GREEN: gpioPortB, pin 0
//   + RGB BLUE:  gpioPortB, pin 2
// =========================================================================
#define SYSTEM_STATUS_LED_PIN 4 // Mặc định dùng kênh RED/BLUE trên BRD2608A
#define SYSTEM_STATUS_LED_PORT gpioPortB
// Task nhấp nháy LED báo trạng thái thiết bị đang hoạt động (Toggle 500ms ON /
// 500ms OFF để tương thích 100% Active-Low & Active-High) Đặt % Cường độ sáng
// mong muốn (Từ 1% đến 100%) Mặc định 10% -> Tiết kiệm pin tối đa và dịu mắt
// khi đeo ngủ ban đêm

#define SYSTEM_STATUS_LED_BRIGHTNESS_PERCENT 10
#include "other_driver/ble_notification_manager.h"

// Task chớp LED báo hiệu hệ thống đang sống (Heartbeat) - Nháy chậm để tiết
// kiệm năng lượng
void LedBlinkyTask(void *pvParameters)
{
  (void)pvParameters;
  printf("--- LED Blinky Task Started ---\r\n");

  // // Cấu hình chân GPIO cho đèn LED Blue (PB02)
  // GPIO_PinModeSet(SL_GPIO_PORT_B, 2, gpioModePushPull, 1);

  // Cấu hình PC08 và PC09
  // GPIO_PinModeSet(gpioPortA, 7, gpioModePushPull, 1);
  GPIO_PinModeSet(gpioPortC, 9, gpioModePushPull, 1);

  while (1)
  {
    // Toggle(Đảo trạng thái)
    GPIO_PinOutToggle(SL_GPIO_PORT_B, 2);

    // GPIO_PinOutToggle(gpioPortA, 7);
    GPIO_PinOutToggle(gpioPortC, 9);

    // Gửi chuỗi Hello world!!! qua BLE mỗi khi LED nháy
    somniguard_ble_send_string("Hello world!!!");

    // Chớp mỗi 1 giây = 1000ms
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// Task log trạng thái và các thông số vận hành FSM
void FsmLoggerTask(void *pvParameters)
{
  somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
  printf("--- SomniGuard FSM Logger Task Started ---\r\n");

  uint32_t log_counter = 0;
  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(500)); // In log mỗi 1 giây
    log_counter++;
    // printf("here");
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

    printf("[FSM LOG #%lu] TopState: %-18s | SubState: %-18s | Finger: %-3s | "
           "SpO2: %2d.%01d%% | BPM: %3d | Motion: %4d mg (%s) | Quiet: %lu ms "
           "| Vib: %u |Valid_res: %u| Buz: %s | BLE: %s | Buffer: %u/%d| \r\n",
           (unsigned long)log_counter, top_str, sub_str,
           fsm->dsp_pro.is_finger_attached ? "YES" : "NO", spo2_i, spo2_d,
           bpm_i, motion_i, fsm->motion_res.is_moving ? "MOVE" : "REST",
           (unsigned long)fsm->quiet_duration_ms, fsm->vibrate_level,
           fsm->dsp_res.signal_valid ? 1 : 0, fsm->buzzer_alarm ? "ON" : "OFF",
           somniguard_ble_is_subscribed() ? "ON" : "OFF", fsm->buffer_pro.count,
           TENSOR_MAX_ROWS);
  }
}

// Task gửi dữ liệu Telemetry 12 bytes định kỳ (1Hz)
void BleTelemetryTask(void *pvParameters)
{
  somniguard_fsm_t *fsm = static_cast<somniguard_fsm_t *>(pvParameters);
  printf("--- BLE Telemetry Task Started ---\r\n");
  uint16_t seq_num = 0;
  uint8_t batt_sample_counter = 0;
  uint8_t current_batt_percent = battery_monitor_get_percent();

  bool batt_low_alert_sent = false;

  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(1000)); // Gửi telemetry mỗi 1 giây liên tục

    // Đo pin định kỳ mỗi 30 giây (30 chu kỳ 1s)
    if (++batt_sample_counter >= 30)
    {
      batt_sample_counter = 0;
      battery_monitor_sample();
      current_batt_percent = battery_monitor_get_percent();
      // Nếu pin yếu (< 20%), phát sự kiện cảnh báo qua BLE Event một lần
      if (battery_monitor_is_low())
      {
        if (!batt_low_alert_sent)
        {
          // printf("[BATTERY] Low Battery Warning: %u%% (%lu mV)\r\n",
          //        current_batt_percent,
          //        (unsigned long)battery_monitor_get_voltage_mv());

          somniguard_ble_notify_event(
              SOMNIGUARD_BLE_EVT_TYPE_POWER_SYSTEM,
              SOMNIGUARD_BLE_EVT_CODE_BATTERY_LOW, current_batt_percent,
              (uint16_t)battery_monitor_get_voltage_mv());

          batt_low_alert_sent = true;
        }
      }
      else
      {
        batt_low_alert_sent = false; // Reset cờ khi pin đã được sạc lại
      }
    }

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
    telem.battery_level = current_batt_percent;

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
      vTaskDelay(pdMS_TO_TICKS(
          1)); // Dùng vTaskDelay thay vì taskYIELD để tránh chiếm dụng 100% CPU
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

#include "other_driver/i2c_ctl.h"

void TestMPU6050Task(void *pvParameters)
{
  (void)pvParameters;
  printf("--- MPU6050 Direct Test Task Started ---\r\n");

  I2CBus mpuI2cBus(I2C0); // Đang dùng I2C0 theo file sl_i2c_sensor_config.h
  MPU6050 mpu(MPU6050_DEFAULT_ADDRESS, &mpuI2cBus);

  // Khởi tạo cảm biến
  mpu.initialize();

  // Cấu hình giống IMU cũ (ICM40627)
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_4); // ±4g
  mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_500); // ±500 dps
  mpu.setDLPFMode(MPU6050_DLPF_BW_20);            // Lọc thông thấp 21Hz (phù hợp định lý
                                                  // Nyquist cho lấy mẫu 50Hz)
  mpu.setRate(19);                                // Tần số lấy mẫu = GyroRate(1kHz) / (1 + 19) = 50Hz

  if (!mpu.testConnection())
  {
    printf("MPU6050 NOT FOUND! Check I2C wiring (SCL: PC05, SDA: PC07) and "
           "power.\r\n");
    while (1)
    {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
  printf("MPU6050 Found & Initialized successfully!\r\n");

  int16_t ax, ay, az, gx, gy, gz;

  while (1)
  {
    // Liên tục kiểm tra dữ liệu từ cảm biến
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // Chuyển đổi sang đơn vị thực tế: Gia tốc (g) và Vận tốc góc (độ/s)
    // Độ nhạy Accel ±4g: 8192 LSB/g. Độ nhạy Gyro ±500dps: 65.5 LSB/dps.
    float accel_x = ax / 8192.0f;
    float accel_y = ay / 8192.0f;
    float accel_z = az / 8192.0f;

    float gyro_x = gx / 65.5f;
    float gyro_y = gy / 65.5f;
    float gyro_z = gz / 65.5f;

    printf(
        "a/g:\t%6.2fg\t%6.2fg\t%6.2fg\t|\t%6.1f dps\t%6.1f dps\t%6.1f dps\r\n",
        accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z);

    // Delay một chút để tránh chiếm dụng toàn bộ CPU (50Hz)
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/* =========================================================================
 * TASK TEST HAPTIC MOTOR (PA07)
 * ========================================================================= */
static void TestHapticMotorTask(void *pvParameters)
{
  (void)pvParameters;

  // 1. Cấp Clock cho GPIO và TIMER0
  CMU_ClockEnable(cmuClock_GPIO, true);
  CMU_ClockEnable(cmuClock_TIMER0, true);

  // 2. Cấu hình chân PA07 làm Output Push-Pull
  GPIO_PinModeSet(gpioPortA, 7, gpioModePushPull, 0);

  // 3. Khởi tạo TIMER0 đếm lên với Prescaler 256
  TIMER_Init_TypeDef timerInit = TIMER_INIT_DEFAULT;
  timerInit.prescale = timerPrescale256;
  timerInit.mode = timerModeUp;
  TIMER_Init(TIMER0, &timerInit);

  // 4. Tính toán giá trị TOP cho tần số PWM 175Hz (Tần số tối ưu cho Motor
  // rung)
  uint32_t timer_clk_freq = CMU_ClockFreqGet(cmuClock_TIMER0);
  uint32_t timer_tick_freq = timer_clk_freq / 256;
  uint32_t pwm_top = (timer_tick_freq / 175) - 1;
  TIMER_TopSet(TIMER0, pwm_top);

  // 5. Cấu hình Channel 0 của TIMER0 sang chế độ PWM
  TIMER_InitCC_TypeDef ccInit = TIMER_INITCC_DEFAULT;
  ccInit.mode = timerCCModePWM;
  TIMER_InitCC(TIMER0, 0, &ccInit);

  // 6. Route tín hiệu PWM CC0 ra chân PA07
  GPIO->TIMERROUTE[0].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN;
  GPIO->TIMERROUTE[0].CC0ROUTE =
      (gpioPortA << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT) |
      (7 << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);

  // Bật TIMER0
  TIMER_CompareSet(TIMER0, 0, 0);
  TIMER_Enable(TIMER0, true);

  printf("\r\n=======================================================\r\n");
  printf(">>> [HAPTIC MOTOR TEST] DANG CHAY TEST PA07 (175Hz) <<<\r\n");
  printf("=======================================================\r\n");

  auto set_duty = [&](uint8_t duty_pct)
  {
    if (duty_pct > 100)
      duty_pct = 100;
    uint32_t compare = (pwm_top * duty_pct) / 100;
    TIMER_CompareBufSet(TIMER0, 0, compare);
  };

  while (1)
  {
    // Giai đoạn 1: Rung kích thích nhẹ 60% Duty trong 2 giây (Cấp 1 - Mild)
    printf("[HAPTIC TEST] 1. Rung nhe ro ret (60%% Duty - Cấp Mild) -> 2 "
           "giay\r\n");
    set_duty(60);
    vTaskDelay(pdMS_TO_TICKS(2000));
    set_duty(0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Giai đoạn 2: Rung vừa 85% Duty trong 2 giây (Cấp 2 - Moderate)
    printf(
        "[HAPTIC TEST] 2. Rung vua (85%% Duty - Cấp Moderate) -> 2 giay\r\n");
    set_duty(85);
    vTaskDelay(pdMS_TO_TICKS(2000));
    set_duty(0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Giai đoạn 3: Rung cực đại 100% Duty trong 2 giây (Cấp 3 - Strong Max)
    printf("[HAPTIC TEST] 3. Rung MAX cong suat (100%% Duty - Cấp Strong) -> 2 "
           "giay\r\n");
    set_duty(100);
    vTaskDelay(pdMS_TO_TICKS(2000));
    set_duty(0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Giai đoạn 4: Ramp-up tăng dần công suất từ 0% -> 100%
    printf("[HAPTIC TEST] 4. Ramp-up tang dan 0%% -> 100%%...\r\n");
    for (int pct = 0; pct <= 100; pct += 10)
    {
      set_duty(pct);
      vTaskDelay(pdMS_TO_TICKS(150));
    }
    set_duty(0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Giai đoạn 5: Rung nhịp búng giật dứt khoát 100% Duty (150ms ON / 50ms
    // OFF) trong 3 giây
    printf("[HAPTIC TEST] 5. Rung nhip giat bung vao da (100%% Duty - 150ms ON "
           "/ 50ms OFF) -> 3 giay\r\n");
    for (int i = 0; i < 15; i++) // 15 chu kỳ x 200ms = 3000ms
    {
      set_duty(100);
      vTaskDelay(pdMS_TO_TICKS(150));
      set_duty(0);
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    set_duty(0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("[HAPTIC TEST] === Xong 1 chu ky test. Nghi 4s truoc khi lap lai "
           "===\r\n\r\n");
    vTaskDelay(pdMS_TO_TICKS(4000));
  }
}

void app_init(void)
{
  printf("========== APP INIT FSM RUN START ==========\r\n");

  // Hiệu ứng LED chạy đuổi báo hiệu hệ thống bắt đầu boot
  somniguard_led_boot_sequence();

  // Khởi tạo BLE Notification Manager
  somniguard_ble_manager_init();

  // Khởi tạo Battery Monitor (IADC0 - PD02)
  battery_monitor_init();

  // // Khởi tạo AI Model
  init_model();

  // Khởi tạo Sensor Hub (Cấu hình IMU & MAX30102 ở 50Hz)
  if (!mySensorHub.initSensors(50))
  {
    printf("WARNING: Failed to initialize SensorHub! Continuing system "
           "boot...\r\n");
  }
  else
  {
    printf("SensorHub initialized successfully.\r\n");
  }

  // Chạy AGC calibration trước khi tạo FSM tasks
  mySensorHub.agcAmplitudeLed();

  // // Khởi tạo Bộ Não FSM
  somniguard_fsm_init(&myFSM, &mySensorHub);

  // 1. Task Thu thập & Xử lý Dữ liệu Cảm biến
  xTaskCreate(DataProcessingTask, "DataProc", 512, &myFSM, tskIDLE_PRIORITY + 3,
              NULL);
  vTaskDelay(pdMS_TO_TICKS(
      50)); // Chờ task in xong startup log trước khi tạo task tiếp theo

  // 2. Task Bộ Não FSM Chính (Top-Level FSM Runner - 100ms)
  xTaskCreate(somniguard_fsm_task, "FsmMain", 512, &myFSM, tskIDLE_PRIORITY + 2,
              NULL);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 3. Sub-FSM Task cho Active Mode
  xTaskCreate(somniguard_active_mode_task, "FsmActive", 384, &myFSM,
              tskIDLE_PRIORITY + 1, NULL);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 4. Sub-FSM Task cho Normal Sleep
  xTaskCreate(somniguard_normal_sleep_task, "FsmSleep", 384, &myFSM,
              tskIDLE_PRIORITY + 1, NULL);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 5. Sub-FSM Task cho Deep Analysis / Can thiệp
  xTaskCreate(somniguard_deep_analysis_task, "FsmDeep", 384, &myFSM,
              tskIDLE_PRIORITY + 1, NULL);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 6. Task Log Trạng Thái FSM & Thông Số Sinh Lý (Commented for low power
  // profiling)
  xTaskCreate(FsmLoggerTask, "FsmLogger", 1024, &myFSM, tskIDLE_PRIORITY + 1,
              NULL);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 7. Task BLE Telemetry Publishing (1Hz / 0.2Hz)
  xTaskCreate(BleTelemetryTask, "BleTelem", 384, &myFSM, tskIDLE_PRIORITY + 1,
              NULL);

  // // 8. Task Test Haptic Motor (PA07) - Chạy trực tiếp
  // xTaskCreate(
  //     TestHapticMotorTask,
  //     "TestHaptic",
  //     512,
  //     NULL,
  //     tskIDLE_PRIORITY + 2,
  //     NULL);

  // // 9. Task AI Benchmark Serial (Lắng nghe Python script)
  // xTaskCreate(
  //     main_app_task,
  //     "AIBenchmark",
  //     1024,
  //     NULL,
  //     tskIDLE_PRIORITY + 3,
  //     NULL);

  // Báo hiệu khởi tạo hệ thống & Tasks thành công (Chớp 2 LED)
  somniguard_led_boot_success();

  printf("========== APP INIT FSM RUN DONE ==========\r\n");
}

void app_process_action(void) {}
