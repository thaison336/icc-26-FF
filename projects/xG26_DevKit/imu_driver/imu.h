#ifndef IMU_H
#define IMU_H

#include "sl_status.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdint.h>

typedef struct imu_data_float {
  float x, y, z;
  float gx, gy, gz;
} imu_data_float_t;

class IMU {
public:
    static IMU& getInstance() {
        static IMU instance;
        return instance;
    }

    IMU(IMU const&) = delete;
    void operator=(IMU const&) = delete;

    // Chỉ cho phép tinh chỉnh Tần số lấy mẫu và Bộ lọc trung bình
    sl_status_t setup(uint16_t sample_rate = 50, uint8_t averaging = 1);

    int available(void);
    bool IMU_getfifo(imu_data_float_t* data_out);
    static void isrCallback(uint8_t int_id, void *ctx);
    void processInterrupt();
    void clearFIFO();
private:
    IMU();

    static const int BUFFER_SIZE = 200;
    typedef struct {
        int16_t x, y, z;
        int16_t gx, gy, gz;
    } imu_raw_data_t;

    imu_raw_data_t buffer[BUFFER_SIZE];
    int head, tail;           
    volatile int count; 

    // Fit cứng Hệ số tỷ lệ vì dải đo đã được chốt (4g và 500dps)
    static constexpr float ACC_SCALE = 8192.0f;
    static constexpr float GYR_SCALE = 65.5f;

    SemaphoreHandle_t imuMutex;
};

float normalize(float x, float mean, float std);

#endif // IMU_H