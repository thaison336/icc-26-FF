#include "imu.h"
#include "MPU6050_driver/MPU6050.h"
#include "other_driver/i2c_ctl.h"
#include "em_core.h"
#include <stdio.h>

static MPU6050 mpuSensor(MPU6050_DEFAULT_ADDRESS, &g_i2c0_bus);

IMU::IMU() : head(0), tail(0), count(0), imuMutex(nullptr) {
    // Không tạo mutex ở đây — constructor của global/static object
    // chạy trước vTaskStartScheduler(), xSemaphoreCreateMutex() sẽ trả về NULL.
    // Mutex được tạo trong setup() sau khi scheduler đã khởi động.
}

sl_status_t IMU::setup(uint16_t sample_rate, uint8_t averaging)
{
    (void)averaging;

    // Tạo mutex tại đây — được gọi sau khi scheduler đã khởi động (từ initSensors)
    if (imuMutex == nullptr) {
        imuMutex = xSemaphoreCreateMutex();
        if (imuMutex == nullptr) {
            printf("[IMU] FATAL: Failed to create imuMutex!\r\n");
            return SL_STATUS_FAIL;
        }
    }

    mpuSensor.initialize();
    mpuSensor.setFullScaleAccelRange(MPU6050_ACCEL_FS_4);
    mpuSensor.setFullScaleGyroRange(MPU6050_GYRO_FS_500);
    mpuSensor.setDLPFMode(MPU6050_DLPF_BW_20);

    // Gyro rate is 1kHz with DLPF enabled, Rate = 1000 / (1 + SMPLRT_DIV)
    uint8_t div = (sample_rate > 0 && sample_rate <= 1000) ? (uint8_t)(1000 / sample_rate - 1) : 19;
    mpuSensor.setRate(div);

    if (!mpuSensor.testConnection()) {
        printf("[IMU] MPU6050 connection test failed! Check I2C wiring (SCL: PC05, SDA: PC07)\r\n");
        return SL_STATUS_FAIL;
    }

    head = 0;
    tail = 0;
    count = 0;
    return SL_STATUS_OK;
}

void IMU::isrCallback(uint8_t int_id, void *ctx)
{
    (void)int_id;
    if (ctx != nullptr) static_cast<IMU*>(ctx)->processInterrupt();
}

void IMU::processInterrupt()
{
    int16_t ax, ay, az, gx, gy, gz;
    mpuSensor.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    buffer[head].x = ax;
    buffer[head].y = ay;
    buffer[head].z = az;
    buffer[head].gx = gx;
    buffer[head].gy = gy;
    buffer[head].gz = gz;

    head = (head + 1) % BUFFER_SIZE;
    if (count < BUFFER_SIZE) count++; else tail = (tail + 1) % BUFFER_SIZE;
}

int IMU::available(void) { return count; }

bool IMU::IMU_getfifo(imu_data_float_t* data_out)
{
    if (imuMutex == nullptr) return false; // Mutex chưa được tạo
    if (xSemaphoreTake(imuMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false; // Timeout 50ms thay vì block mãi mãi

    CORE_DECLARE_IRQ_STATE;
    CORE_ENTER_CRITICAL();

    if (count == 0) {
        CORE_EXIT_CRITICAL();
        xSemaphoreGive(imuMutex);
        return false;
    }

    data_out->x  = buffer[tail].x  / ACC_SCALE;
    data_out->y  = buffer[tail].y  / ACC_SCALE;
    data_out->z  = buffer[tail].z  / ACC_SCALE;
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
}

float normalize(float x, float mean, float std) {
    return (x - mean) / (std + 1e-8f);
}