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
        // Tạo interrupt task ngay từ đầu để driver I2C được xử lý đúng cách
        // AGC sẽ đọc từ software buffer mà task này fill vào
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
    else
    {
        xTaskCreate(MAX30102TaskINT, "MAX30102_Task", 256, (void *)&m_max30102, tskIDLE_PRIORITY + 3, &m_max30102TaskHandle);
    }
}

//// AGC amplitude current Led RED and IR by raw IR and Red

void SensorHub::agcAmplitudeLed()
{
    // KHÔNG suspend interrupt task - để task đó tiếp tục fill software buffer qua I2C
    // AGC chỉ đọc từ software buffer (thread-safe) và queue LED commands cho task xử lý

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

    // Khởi tạo dòng LED ban đầu qua manager queue (interrupt task sẽ ghi xuống hardware)
    uint8_t current_red_amp = 60;
    uint8_t current_ir_amp = 60;
    m_max30102.setPulseAmplitudeRed(current_red_amp);
    m_max30102.setPulseAmplitudeIR(current_ir_amp);
    // Xả FIFO qua manager (interrupt task xử lý) để bắt đầu từ trạng thái sạch
    m_max30102.clearFIFO();
    m_max30102.setSampleRate(this->max30102_freq);

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

    // 2. AGC Calibration - đọc từ software buffer, queue LED commands cho interrupt task
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

        // Đọc mẫu cuối cùng trong buffer (đã được interrupt task fill)
        uint32_t ppgRed = 0, ppgIR = 0;
        // Xả hết trừ 1 mẫu cuối để lấy giá trị mới nhất
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

        // Điều chỉnh RED
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

        // Điều chỉnh IR
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
            // Queue lệnh qua manager - interrupt task sẽ apply vào hardware và clearFIFO tự động
            m_max30102.setPulseAmplitudeRed(current_red_amp);
            m_max30102.setPulseAmplitudeIR(current_ir_amp);
        }
    }

    printf("[AGC] DONE: RED Amp=0x%02X, IR Amp=0x%02X\r\n",
           current_red_amp, current_ir_amp);
    // Interrupt task vẫn đang chạy, không cần resume
}
