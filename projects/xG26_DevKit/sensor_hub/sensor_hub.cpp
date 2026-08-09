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

static I2CBus max30102I2CBus(I2C1);

bool initMax30102(MAX30102_manager &MAX30102Sensor, int samplerate)
{

    if (!MAX30102Sensor.begin(&max30102I2CBus))
    {
        printf("Failed to initialize MAX30102!\r\n");
        return false;
    }

    MAX30102Sensor.driver().setup(0x1F, MAX30102_AVERAGING, 2, samplerate * MAX30102_AVERAGING, 411, 4096);
    MAX30102Sensor.driver().setFIFOAlmostFull(7);
    MAX30102Sensor.driver().enableAFULL();
    // MAX30102Sensor.driver().enableDATARDY();
    MAX30102Sensor.driver().Max30102_setSampleRate(samplerate * MAX30102_AVERAGING);
    MAX30102Sensor.driver().debugDumpConfig();
    return true;
}

bool initIMU(IMU &imu, int samplerate)
{
    sl_status_t imu_status = imu.setup(samplerate, IMU_AVERAGING);
    if (imu_status != SL_STATUS_OK)
    {
        printf("Failed to initialize IMU!\r\n");
        return false;
    }
    return true;
}

void MAX30102TaskINT(void *pvParameters)
{
    MAX30102_manager *manager = static_cast<MAX30102_manager *>(pvParameters);
    init_MAX30102_Interrupt(manager);
    manager->task();
}
//////////////////////////////////////////////////////////////////////////////////////

bool SensorHub::initSensors(uint16_t sampleRate)
{
    this->max30102_freq = sampleRate * MAX30102_AVERAGING;
    if (initMax30102(this->m_max30102, sampleRate) && initIMU(this->m_imu, sampleRate))
    {
        xTaskCreate(MAX30102TaskINT, "MAX30102_Task", 256, (void *)&m_max30102, tskIDLE_PRIORITY + 3, &m_max30102TaskHandle);
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
    imu_data_float_t imu_out;
    if (m_max30102.available() == 0)
    {
        // printf("\r\nNo data from MAX30102!\r\n");
        return false;
    }
    if (m_imu.available() == 0)
    {
        // Đã comment printf để tránh bị ngập lụt log khi bị lệch pha
        // printf("\r\nNo data from IMU!\r\n");
        return false;
    }

    m_imu.IMU_getfifo(&imu_out);
    data->ax = imu_out.x;
    data->ay = imu_out.y;
    data->az = imu_out.z;
    data->gx = imu_out.gx;
    data->gy = imu_out.gy;
    data->gz = imu_out.gz;

    data->ppg_red = m_max30102.getFIFORed();
    data->ppg_ir = m_max30102.getFIFOIR();
    m_max30102.nextSample();
    return true;
}
bool SensorHub::getsensordata(sensor_hub_data_t *data)
{
    if (data == nullptr)
        return false;
    imu_data_float_t imu_out;

    // --- ĐỒNG BỘ HÓA (RESYNC) THỜI GIAN THỰC ---
    // Đặt ngưỡng xả là 30 mẫu vì MAX30102 đọc data theo cục (Burst Read ~25 mẫu/ngắt).
    // Nếu chênh lệch < 30 thì chỉ là do lệch pha đọc, nhưng nếu > 30 chắc chắn là do Clock Drift.
    const int THRESHOLD = 30;

    // Biến static để lưu thời điểm xả mẫu lần trước (tính bằng ms)
    static uint32_t last_imu_discard_time = 0;
    static uint32_t last_max_discard_time = 0;

    // Nếu IMU chạy nhanh hơn và đọng nhiều hơn MAX30102
    int imu_discard_count = 0;
    while ((int)m_imu.available() - (int)m_max30102.available() > THRESHOLD)
    {
        imu_data_float_t trash;
        m_imu.IMU_getfifo(&trash); // Xả bỏ 1 mẫu cũ nhất của IMU
        imu_discard_count++;
    }
    if (imu_discard_count > 0)
    {
        uint32_t current_time = xTaskGetTickCount();
        uint32_t diff = (last_imu_discard_time == 0) ? 0 : (current_time - last_imu_discard_time);
        printf("\r\n[RESYNC] IMU too fast! Discarded %d samples. Time since last discard: %lu ms\r\n", imu_discard_count, diff);
        last_imu_discard_time = current_time;
    }

    // Nếu MAX30102 chạy nhanh hơn IMU
    int max_discard_count = 0;
    while ((int)m_max30102.available() - (int)m_imu.available() > THRESHOLD)
    {
        m_max30102.nextSample(); // Xả bỏ 1 mẫu cũ nhất của MAX
        max_discard_count++;
    }
    if (max_discard_count > 0)
    {
        uint32_t current_time = xTaskGetTickCount();
        uint32_t diff = (last_max_discard_time == 0) ? 0 : (current_time - last_max_discard_time);
        printf("\r\n[RESYNC] MAX30102 too fast! Discarded %d samples. Time since last discard: %lu ms\r\n", max_discard_count, diff);
        last_max_discard_time = current_time;
    }
    // ------------------------------------------

    if (m_max30102.available() == 0)
    {
        // printf("\r\nNo data from MAX30102!\r\n");
        return false;
    }
    if (m_imu.available() == 0)
    {
        // Đã comment printf để tránh bị ngập lụt log khi bị lệch pha
        // printf("\r\nNo data from IMU!\r\n");
        return false;
    }

    m_imu.IMU_getfifo(&imu_out);
    data->ax = imu_out.x;
    data->ay = imu_out.y;
    data->az = imu_out.z;
    data->gx = imu_out.gx;
    data->gy = imu_out.gy;
    data->gz = imu_out.gz;

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
}

//// AGC amplitude current Led RED and IR by raw IR and Red

void SensorHub::agcAmplitudeLed()
{
    // Tạm dừng Task interrupt để tránh xung đột I2C bus
    suspendInterruptTask();

    uint32_t full_scale_adc = m_max30102.getADCrange();
    printf("[AGC] Max ADC range: %lu\r\n", full_scale_adc);

    const uint32_t SAMPLE_PERIOD_MS = 100;
    const uint32_t FINGER_THRESHOLD = 30000; // Ngưỡng tín hiệu tối thiểu để xác nhận có tay trên sensor
    uint32_t agc_cooldown_counter = 0;
    uint8_t current_red_amp = 0x1F;
    uint8_t current_ir_amp = 0x1F;

    m_max30102.driver().setPulseAmplitudeRed(current_red_amp);
    m_max30102.driver().setPulseAmplitudeIR(current_ir_amp);
    m_max30102.driver().clearFIFO(); // clearFIFO() đã tự clear INT latch, không cần setSampleRate() sau đây nữa

    // 1. Kiểm tra chắc chắn người dùng đã đặt tay lên sensor trước khi bắt đầu AGC
    printf("[AGC] Waiting for finger to be placed on sensor...\r\n");
    while (true)
    {
        m_max30102.driver().check();
        if (m_max30102.driver().available() > 1)
        {
            m_max30102.driver().nextSample();
            uint32_t checkRed = m_max30102.driver().getFIFORed();
            uint32_t checkIR = m_max30102.driver().getFIFOIR();
            m_max30102.driver().nextSample();

            if (checkIR >= FINGER_THRESHOLD || checkRed >= FINGER_THRESHOLD)
            {
                printf("[AGC] Finger detected (IR: %lu, RED: %lu). Starting AGC...\r\n",
                       (unsigned long)checkIR, (unsigned long)checkRed);
                break;
            }
            else
            {
                printf("[AGC] No finger detected (IR: %lu < %lu). Waiting...\r\n",
                       (unsigned long)checkIR, (unsigned long)FINGER_THRESHOLD);
            }
        }
        m_max30102.driver().clearFIFO();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // 2. Tiến hành AGC khi đã có tay trên sensor
    while (agc_cooldown_counter < SAMPLE_PERIOD_MS)
    {
        uint32_t ppgRed = 0, ppgIR = 0;
        m_max30102.driver().check();
        if (m_max30102.driver().available() > 1)
        {
            m_max30102.driver().nextSample();
            ppgRed = m_max30102.driver().getFIFORed();
            ppgIR = m_max30102.driver().getFIFOIR();
            m_max30102.driver().nextSample();
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Tạm dừng điều chỉnh nếu người dùng nhấc tay khỏi sensor giữa chừng
        if (ppgIR < FINGER_THRESHOLD && ppgRed < FINGER_THRESHOLD)
        {
            printf("[AGC] Finger removed during AGC! Pausing adjustment...\r\n");
            m_max30102.driver().clearFIFO();
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        bool is_hardware_adjusted = false;
        printf("%lu, %lu, %u, %u.\r\n", (unsigned long)ppgRed, (unsigned long)ppgIR, current_red_amp, current_ir_amp);

        // Chỉnh dựa trên dc_track thay vì raw để không bị méo biên độ bởi sóng AC
        if (ppgRed > 0.6f * full_scale_adc && current_red_amp > 10)
        {
            if (ppgRed > 240000.0f)
                current_red_amp -= 5;
            else
            {
                current_red_amp--;
            }

            is_hardware_adjusted = true;
        }
        else if (ppgRed < 0.4f * full_scale_adc && current_red_amp < 245)
        {
            current_red_amp++;
            is_hardware_adjusted = true;
        }

        if (ppgIR > 0.6f * full_scale_adc && current_ir_amp > 10)
        {
            if (ppgIR > 240000.0f)
                current_ir_amp -= 5;
            else
            {
                current_ir_amp--;
            }

            is_hardware_adjusted = true;
        }
        else if (ppgIR < 0.4f * full_scale_adc && current_ir_amp < 245)
        {
            current_ir_amp++;
            is_hardware_adjusted = true;
        }

        if (is_hardware_adjusted)
        {
            m_max30102.driver().setPulseAmplitudeRed(current_red_amp);
            m_max30102.driver().setPulseAmplitudeIR(current_ir_amp);
        }

        m_max30102.driver().clearFIFO();
        agc_cooldown_counter++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    printf("the final amp red: %u | the final amp ir: %u\r\n", current_red_amp, current_ir_amp);

    // Phục hồi Task interrupt sau khi AGC hoàn thành
    m_max30102.driver().clearFIFO();
    resumeInterruptTask();
}