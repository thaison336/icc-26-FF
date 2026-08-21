#include "sensor_hub.h"
#include <stdio.h>
#include "i2c_ctl.h"
#include "FreeRTOS.h"
#include "task.h"
#define MAX30102_AVERAGING 1
#define IMU_AVERAGING 1
MAX30102_manager &SensorHub::MAX30102_driver()
{
    return m_max30102;
}

IMU &SensorHub::imu_driver()
{
    return m_imu;
}

////////////////////////////INIT sub function/////////////////////////////////////////////////////////////////////////////

bool initMax30102(MAX30102_manager &MAX30102Sensor, int samplerate)
{
    if (!MAX30102Sensor.begin(&g_i2c0_bus))
    {
        printf("Failed to initialize MAX30102!\r\n");
        return false;
    }

    MAX30102Sensor.driver().setup(0x1F, MAX30102_AVERAGING, 2, samplerate * MAX30102_AVERAGING, 411, 4096);
    MAX30102Sensor.driver().setFIFOAlmostFull(7);
    MAX30102Sensor.driver().enableAFULL();
    // MAX30102Sensor.driver().enableDATARDY(); // Bật ngắt DATA READY (50Hz: mỗi 20ms tạo 1 xung ngắt trên chân INT)
    MAX30102Sensor.driver().Max30102_setSampleRate(samplerate * MAX30102_AVERAGING);
    MAX30102Sensor.driver().debugDumpConfig();
    return true;
}

bool initIMU(IMU &imu, int samplerate)
{
    sl_status_t imu_status = imu.setup(samplerate, 1);
    if (imu_status != SL_STATUS_OK)
    {
        printf("Failed to initialize MPU6050 IMU!\r\n");
        return false;
    }
    printf("MPU6050 IMU initialized successfully.\r\n");
    return true;
}

void MAX30102TaskINT(void *pvParameters)
{
    MAX30102_manager *manager = static_cast<MAX30102_manager *>(pvParameters);
    printf("--- MAX30102 Interrupt Task Started ---\r\n");
    init_MAX30102_Interrupt(manager);
    manager->task();
}
//////////////////////////////////////////////////////////////////////////////////////

bool SensorHub::initSensors(uint16_t sampleRate)
{
    this->max30102_freq = sampleRate * MAX30102_AVERAGING;
    if (initIMU(this->m_imu, sampleRate) && initMax30102(this->m_max30102, sampleRate))
    {
        xTaskCreate(MAX30102TaskINT, "MAX30102_Task", 512, (void *)&m_max30102, tskIDLE_PRIORITY + 3, &m_max30102TaskHandle);
        resetSensor();
        vTaskDelay(pdMS_TO_TICKS(10));
        return true;
    }
    return false;
}
bool SensorHub::getPPGdata(sensor_hub_data_t *data)
{
    if (data == nullptr)
        return false;

    if (m_max30102.available() == 0)
    {
        return false;
    }

    data->ppg_red = m_max30102.getFIFORed();
    data->ppg_ir = m_max30102.getFIFOIR();
    m_max30102.nextSample();
    return true;
}
bool SensorHub::getsensordata(sensor_hub_data_t *data)
{
    if (data == nullptr)
        return false;

    if (m_max30102.available() == 0)
    {
        return false;
    }

    static imu_data_float_t last_imu_out = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    if (m_imu.available() > 0)
    {
        m_imu.IMU_getfifo(&last_imu_out);
    }

    data->ax = last_imu_out.x;
    data->ay = last_imu_out.y;
    data->az = last_imu_out.z;
    data->gx = last_imu_out.gx;
    data->gy = last_imu_out.gy;
    data->gz = last_imu_out.gz;

    data->ppg_red = m_max30102.getFIFORed();
    data->ppg_ir = m_max30102.getFIFOIR();
    m_max30102.nextSample();
    return true;
}

//// Reset MAX30102 truoc IMU, khi reset MAX30102 luon de ham setSampleRate sau cung
void SensorHub::resetSensor()
{
    m_max30102.clearFIFO();
    m_max30102.setSampleRate(this->max30102_freq);
    m_imu.clearFIFO();
}

void SensorHub::suspendInterruptTask()
{
    if (m_max30102TaskHandle != nullptr)
    {
        vTaskSuspend(m_max30102TaskHandle);
    }
}

void SensorHub::resumeInterruptTask()
{
    if (m_max30102TaskHandle != nullptr)
    {
        vTaskResume(m_max30102TaskHandle);
    }
    else
    {
        xTaskCreate(MAX30102TaskINT, "MAX30102_Task", 256, (void *)&m_max30102, tskIDLE_PRIORITY + 3, &m_max30102TaskHandle);
    }
}

//// AGC amplitude current Led RED and IR by raw IR and Red

void SensorHub::agcAmplitudeLed()
{
    // KHÃ”NG suspend interrupt task - Ä‘á»ƒ task Ä‘Ã³ tiáº¿p tá»¥c fill software buffer qua I2C
    // AGC chá»‰ Ä‘á»c tá»« software buffer (thread-safe) vÃ  queue LED commands cho task xá»­ lÃ½

    uint32_t full_scale_adc = m_max30102.getADCrange();
    if (full_scale_adc == 0)
    {
        full_scale_adc = 262143; // 18-bit ADC fallback
    }
    printf("[AGC] Max ADC range: %lu\r\n", (unsigned long)full_scale_adc);

    const uint32_t FINGER_THRESHOLD = 30000;
    const uint32_t TARGET_MIN = (uint32_t)(0.40f * full_scale_adc);
    const uint32_t TARGET_MAX = (uint32_t)(0.60f * full_scale_adc);
    const uint32_t HIGH_SATURATION = (uint32_t)(0.90f * full_scale_adc);

    // Khá»Ÿi táº¡o dÃ²ng LED ban Ä‘áº§u qua manager queue (interrupt task sáº½ ghi xuá»‘ng hardware)
    uint8_t current_red_amp = 110;
    uint8_t current_ir_amp = 110;
    m_max30102.setPulseAmplitudeRed(current_red_amp);
    m_max30102.setPulseAmplitudeIR(current_ir_amp);
    // Xả FIFO qua manager (interrupt task xử lý) để bắt đầu từ trạng thái sạch
    m_max30102.clearFIFO();
    m_max30102.setSampleRate(this->max30102_freq);
    m_max30102.enableDATARDY(); // Bật ngắt từng mẫu để AGC phản hồi nhanh nhạy khi calib

    // 1. Chờ interrupt task đọc đủ dữ liệu và phát hiện tay đặt vào
    printf("[AGC] Waiting for finger to be placed on sensor...\r\n");

    uint32_t wait_print_counter = 0;
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100));

        uint8_t avail = m_max30102.available();

        if (avail > 0)
        {
            uint32_t checkRed = m_max30102.getFIFORed();
            uint32_t checkIR = m_max30102.getFIFOIR();
            m_max30102.nextSample();

            if (checkIR >= FINGER_THRESHOLD || checkRed >= FINGER_THRESHOLD)
            {
                printf("[AGC] Finger detected! (IR: %lu, RED: %lu). Starting AGC calibration...\r\n",
                       (unsigned long)checkIR, (unsigned long)checkRed);
                m_max30102.clearFIFO();
                m_max30102.setSampleRate(this->max30102_freq);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            }

            if (++wait_print_counter % 10 == 0)
            {
                printf("[AGC] Waiting for finger... (IR: %lu, RED: %lu < %lu)\r\n",
                       (unsigned long)checkIR, (unsigned long)checkRed,
                       (unsigned long)FINGER_THRESHOLD);
            }
        }
        else
        {
            if (++wait_print_counter % 10 == 0)
            {
                printf("[AGC] Waiting for sensor data...\r\n");
            }
        }
    }

    // 2. AGC Calibration - Ä‘á» c tá»« software buffer, queue LED commands cho interrupt task
    printf("[AGC] Calibrating LED amplitudes (Target: %lu - %lu)...\r\n",
           (unsigned long)TARGET_MIN, (unsigned long)TARGET_MAX);

    uint32_t stable_count = 0;
    const uint32_t MAX_AGC_ITERATIONS = 60;
    uint32_t iteration = 0;

    while (iteration < MAX_AGC_ITERATIONS)
    {
        iteration++;
        vTaskDelay(pdMS_TO_TICKS(100));

        uint8_t avail = m_max30102.available();
        if (avail == 0)
        {
            continue;
        }

        // Äá»c máº«u cuá»‘i cÃ¹ng trong buffer (Ä‘Ã£ Ä‘Æ°á»£c interrupt task fill)
        uint32_t ppgRed = 0, ppgIR = 0;
        // Xáº£ háº¿t trá»« 1 máº«u cuá»‘i Ä‘á»ƒ láº¥y giÃ¡ trá»‹ má»›i nháº¥t
        while (m_max30102.available() > 1)
        {
            m_max30102.nextSample();
        }
        ppgRed = m_max30102.getFIFORed();
        ppgIR = m_max30102.getFIFOIR();
        m_max30102.nextSample();

        if (ppgIR < FINGER_THRESHOLD && ppgRed < FINGER_THRESHOLD)
        {
            printf("[AGC] Finger removed! Pausing...\r\n");
            stable_count = 0;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        bool is_red_ok = (ppgRed >= TARGET_MIN && ppgRed <= TARGET_MAX);
        bool is_ir_ok = (ppgIR >= TARGET_MIN && ppgIR <= TARGET_MAX);

        printf("[AGC #%lu] RED=%lu (Amp=0x%02X) IR=%lu (Amp=0x%02X)\r\n",
               (unsigned long)iteration, (unsigned long)ppgRed, current_red_amp,
               (unsigned long)ppgIR, current_ir_amp);

        if (is_red_ok && is_ir_ok)
        {
            stable_count++;
            if (stable_count >= 3)
            {
                printf("[AGC] CONVERGED & STABLE!\r\n");
                break;
            }
        }
        else
        {
            stable_count = 0;
        }

        bool changed = false;

        // Äiá»u chá»‰nh RED
        if (ppgRed > HIGH_SATURATION && current_red_amp > 5)
        {
            current_red_amp = (current_red_amp > 15) ? (current_red_amp - 10) : 1;
            changed = true;
        }
        else if (ppgRed > TARGET_MAX && current_red_amp > 1)
        {
            uint8_t step = (ppgRed - TARGET_MAX > 30000) ? 3 : 1;
            current_red_amp = (current_red_amp > step) ? (current_red_amp - step) : 1;
            changed = true;
        }
        else if (ppgRed < TARGET_MIN && current_red_amp < 0xFE)
        {
            uint8_t step = (TARGET_MIN - ppgRed > 30000) ? 3 : 1;
            current_red_amp = (current_red_amp + step > 0xFF) ? 0xFF : (current_red_amp + step);
            changed = true;
        }

        // Äiá»u chá»‰nh IR
        if (ppgIR > HIGH_SATURATION && current_ir_amp > 5)
        {
            current_ir_amp = (current_ir_amp > 15) ? (current_ir_amp - 10) : 1;
            changed = true;
        }
        else if (ppgIR > TARGET_MAX && current_ir_amp > 1)
        {
            uint8_t step = (ppgIR - TARGET_MAX > 30000) ? 3 : 1;
            current_ir_amp = (current_ir_amp > step) ? (current_ir_amp - step) : 1;
            changed = true;
        }
        else if (ppgIR < TARGET_MIN && current_ir_amp < 0xFE)
        {
            uint8_t step = (TARGET_MIN - ppgIR > 30000) ? 3 : 1;
            current_ir_amp = (current_ir_amp + step > 0xFF) ? 0xFF : (current_ir_amp + step);
            changed = true;
        }

        if (changed)
        {
            // Queue lá»‡nh qua manager - interrupt task sáº½ apply vÃ o hardware vÃ  clearFIFO tá»± Ä‘á»™ng
            m_max30102.setPulseAmplitudeRed(current_red_amp);
            m_max30102.setPulseAmplitudeIR(current_ir_amp);
        }
    }

    printf("[AGC] DONE: RED Amp=0x%02X, IR Amp=0x%02X\r\n",
           current_red_amp, current_ir_amp);
    // AGC hoàn tất: Tắt ngắt DATARDY (1 mẫu), chuyển sang ngắt AFULL (batch 25 mẫu) để tối ưu pin & I2C
    m_max30102.disableDATARDY();
    m_max30102.clearFIFO();
    m_max30102.setSampleRate(this->max30102_freq);
}
