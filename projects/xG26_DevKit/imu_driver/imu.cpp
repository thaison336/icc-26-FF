#include "imu.h"
#include "MPU6050_driver/MPU6050.h"
#include "other_driver/i2c_ctl.h"
#include "em_core.h"
#include <stdio.h>

static MPU6050 mpuSensor(MPU6050_DEFAULT_ADDRESS, &g_i2c0_bus);

IMU::IMU() : head(0), tail(0), count(0), imuMutex(nullptr)
{
    // Không tạo mutex ở đây — constructor của global/static object
    // chạy trước vTaskStartScheduler(), xSemaphoreCreateMutex() sẽ trả về NULL.
    // Mutex được tạo trong setup() sau khi scheduler đã khởi động.
}

sl_status_t IMU::setup(uint16_t sample_rate, uint8_t averaging)
{
    (void)averaging;

    // Tạo mutex tại đây — được gọi sau khi scheduler đã khởi động (từ initSensors)
    if (imuMutex == nullptr)
    {
        imuMutex = xSemaphoreCreateMutex();
        if (imuMutex == nullptr)
        {
            printf("[IMU] FATAL: Failed to create imuMutex!\r\n");
            return SL_STATUS_FAIL;
        }
    }

    mpuSensor.initialize();
    mpuSensor.setClockSource(MPU6050_CLOCK_INTERNAL); // Dùng xung nội 8MHz vì Gyro ở Standby
    mpuSensor.setFullScaleAccelRange(MPU6050_ACCEL_FS_4);
    mpuSensor.setFullScaleGyroRange(MPU6050_GYRO_FS_500);
    mpuSensor.setDLPFMode(MPU6050_DLPF_BW_20);

    // Tắt hoàn toàn 3 trục Gyroscope đưa vào Standby mode (tiết kiệm ~3.6mA)
    mpuSensor.setStandbyXGyroEnabled(true);
    mpuSensor.setStandbyYGyroEnabled(true);
    mpuSensor.setStandbyZGyroEnabled(true);

    // Gyro rate is 1kHz with DLPF enabled, Rate = 1000 / (1 + SMPLRT_DIV)
    uint8_t div = (sample_rate > 0 && sample_rate <= 1000) ? (uint8_t)(1000 / sample_rate - 1) : 19;
    mpuSensor.setRate(div);

    if (!mpuSensor.testConnection())
    {
        printf("[IMU] MPU6050 connection test failed! Check I2C wiring (SCL: PC05, SDA: PC07)\r\n");
        return SL_STATUS_FAIL;
    }

    // Kích hoạt Hardware FIFO trên MPU6050 chỉ cho Accel (3 trục: 6 bytes/mẫu)
    mpuSensor.setFIFOEnabled(false);
    mpuSensor.resetFIFO();
    mpuSensor.setAccelFIFOEnabled(true);
    mpuSensor.setXGyroFIFOEnabled(false);
    mpuSensor.setYGyroFIFOEnabled(false);
    mpuSensor.setZGyroFIFOEnabled(false);
    mpuSensor.setFIFOEnabled(true);

    head = 0;
    tail = 0;
    count = 0;
    return SL_STATUS_OK;
}

void IMU::sleep()
{
    mpuSensor.setSleepEnabled(true);
}

void IMU::wakeup()
{
    // Chỉ đánh thức chip, chưa configure lại — gọi setup() sau nếu cần
    mpuSensor.setSleepEnabled(false);
}

void IMU::isrCallback(uint8_t int_id, void *ctx)
{
    (void)int_id;
    if (ctx != nullptr)
        static_cast<IMU *>(ctx)->processInterrupt();
}

void IMU::processInterrupt()
{
    uint16_t fifo_count = mpuSensor.getFIFOCount();

    // Nếu FIFO bị tràn (>= 1024 bytes) hoặc có lỗi, thực hiện chu trình Reset & Khôi phục chuẩn của MPU6050
    if (fifo_count >= 1024)
    {
        mpuSensor.setFIFOEnabled(false);
        mpuSensor.resetFIFO();
        mpuSensor.setFIFOEnabled(true);
        fifo_count = 0;
    }

    if (fifo_count >= 6)
    {
        uint8_t packet[6];
        uint8_t max_packets = 50; // Giới hạn tối đa 50 packet/ngắt tránh treo I2C
        while (fifo_count >= 6 && max_packets-- > 0)
        {
            mpuSensor.getFIFOBytes(packet, 6);
            fifo_count -= 6;

            int16_t ax = (int16_t)(((uint16_t)packet[0] << 8) | packet[1]);
            int16_t ay = (int16_t)(((uint16_t)packet[2] << 8) | packet[3]);
            int16_t az = (int16_t)(((uint16_t)packet[4] << 8) | packet[5]);

            CORE_DECLARE_IRQ_STATE;
            CORE_ENTER_CRITICAL();

            buffer[head].x = ax;
            buffer[head].y = ay;
            buffer[head].z = az;
            buffer[head].gx = 0;
            buffer[head].gy = 0;
            buffer[head].gz = 0;

            head = (head + 1) % BUFFER_SIZE;
            if (count < BUFFER_SIZE)
                count++;
            else
                tail = (tail + 1) % BUFFER_SIZE;

            CORE_EXIT_CRITICAL();
        }
    }
    else
    {
        // Fallback: nếu FIFO chưa có packet hoặc đang khởi động, đọc trực tiếp 1 mẫu từ thanh ghi gia tốc
        int16_t ax, ay, az;
        mpuSensor.getAcceleration(&ax, &ay, &az);

        CORE_DECLARE_IRQ_STATE;
        CORE_ENTER_CRITICAL();

        buffer[head].x = ax;
        buffer[head].y = ay;
        buffer[head].z = az;
        buffer[head].gx = 0;
        buffer[head].gy = 0;
        buffer[head].gz = 0;

        head = (head + 1) % BUFFER_SIZE;
        if (count < BUFFER_SIZE)
            count++;
        else
            tail = (tail + 1) % BUFFER_SIZE;

        CORE_EXIT_CRITICAL();
    }
}

int IMU::available(void) { return count; }

bool IMU::IMU_getfifo(imu_data_float_t *data_out)
{
    if (imuMutex == nullptr)
        return false; // Mutex chưa được tạo
    if (xSemaphoreTake(imuMutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return false; // Timeout 50ms thay vì block mãi mãi

    CORE_DECLARE_IRQ_STATE;
    CORE_ENTER_CRITICAL();

    if (count == 0)
    {
        CORE_EXIT_CRITICAL();
        xSemaphoreGive(imuMutex);
        return false;
    }

    data_out->x = buffer[tail].x / ACC_SCALE;
    data_out->y = buffer[tail].y / ACC_SCALE;
    data_out->z = buffer[tail].z / ACC_SCALE;
    data_out->gx = buffer[tail].gx / GYR_SCALE;
    data_out->gy = buffer[tail].gy / GYR_SCALE;
    data_out->gz = buffer[tail].gz / GYR_SCALE;

    tail = (tail + 1) % BUFFER_SIZE;
    count--;

    CORE_EXIT_CRITICAL();
    xSemaphoreGive(imuMutex);

    return true;
}

void IMU::clearFIFO()
{
    CORE_DECLARE_IRQ_STATE;
    CORE_ENTER_CRITICAL();

    head = 0;
    tail = 0;
    count = 0;

    CORE_EXIT_CRITICAL();
    mpuSensor.setFIFOEnabled(false);
    mpuSensor.resetFIFO();
    mpuSensor.setFIFOEnabled(true);
}

float normalize(float x, float mean, float std)
{
    return (x - mean) / (std + 1e-8f);
}