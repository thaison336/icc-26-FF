#ifndef SENSOR_HUB_H
#define SENSOR_HUB_H

#include "MAX30102_manager.h"
#include "imu.h"

// Cấu trúc dữ liệu để lấy tất cả từ sensor hub (tuỳ chọn)
typedef struct
{
    float ax, ay, az;
    float gx, gy, gz;
    uint32_t ppg_red;
    uint32_t ppg_ir;
} sensor_hub_data_t;

class SensorHub
{
public:
    SensorHub() : m_imu(IMU::getInstance()), m_max30102TaskHandle(nullptr) {}
    MAX30102_manager &MAX30102_driver();
    IMU &imu_driver();

    // 1. Khởi tạo các sensor với tham số mặc định (ngoại trừ sample rate)
    // - MAX30102: powerLevel=0x1F, sampleAverage=4, ledMode=3, pulseWidth=411, adcRange=4096
    // - IMU: averaging=1
    bool initSensors(uint16_t sampleRate = 50);

    bool getsensordata(sensor_hub_data_t *data);

    void resetSensor();
    //=======================new Function of N ==========================//
    bool getPPGdata(sensor_hub_data_t *data);
    void agcAmplitudeLed();

    // Tạm dừng / Phục hồi MAX30102 Interrupt Task để tránh xung đột I2C bus
    void suspendInterruptTask();
    void resumeInterruptTask();

private:
    uint16_t max30102_freq;
    MAX30102_manager m_max30102;
    IMU &m_imu;
    TaskHandle_t m_max30102TaskHandle;
};

#endif // SENSOR_HUB_H
