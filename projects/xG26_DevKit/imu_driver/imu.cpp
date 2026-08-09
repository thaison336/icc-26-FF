#include "imu.h"
#include "sl_imu.h"
#include "sl_gpio.h"
#include "em_core.h"
#include "sl_icm40627.h" 

#if defined (SL_CATALOG_ICM40627_DRIVER_PRESENT)
#include "sl_icm40627_config.h"
#define  SL_IMU_INT_PORT SL_ICM40627_INT_PORT
#define  SL_IMU_INT_PIN  SL_ICM40627_INT_PIN
#else
#error "Missing ICM-40627 configuration"
#endif

#include "sl_board_control.h"
IMU::IMU() : head(0), tail(0), count(0) {
    imuMutex = xSemaphoreCreateMutex();
}

sl_status_t IMU::setup(uint16_t sample_rate, uint8_t averaging)
{
    // 1. Bật nguồn phần cứng cho cảm biến trên board xG26
    sl_board_enable_sensor(SL_BOARD_SENSOR_IMU);
    vTaskDelay(pdMS_TO_TICKS(10)); // Đợi điện áp ổn định
    // 1. Dùng hàm khởi tạo gốc
    sl_status_t status = sl_icm40627_init();
    if (status != SL_STATUS_OK) return status;

    // 2. LỆNH SỐNG CÒN: Bật nguồn cho Accel, Gyro và Cảm biến nhiệt độ
    sl_icm40627_enable_sensor(true, true, true);

    // 3. Ép dải đo chuẩn xác 
    sl_icm40627_accel_set_full_scale(sl_accelFS_4g);    
    sl_icm40627_gyro_set_full_scale(sl_gyroFS_500dps);  

    // 4. Ép tần số lấy mẫu 
    sl_icm40627_set_sample_rate((float)sample_rate);

    // -------------------------------------------------------------
    // 4.5. Ép bộ lọc Averaging (Digital Low Pass Filter)
    // -------------------------------------------------------------
    sl_accel_BW_t acc_bw;
    sl_gyro_BW_t gyr_bw;

    switch (averaging) {
        case 1:  
            acc_bw = sl_accelBW_ODR_DIV_1;  gyr_bw = sl_gyroBW_ODR_DIV_1;  
            break;
        case 2:  
            acc_bw = sl_accelBW_ODR_DIV_2;  gyr_bw = sl_gyroBW_ODR_DIV_2;  
            break;
        case 4:  
            acc_bw = sl_accelBW_ODR_DIV_4;  gyr_bw = sl_gyroBW_ODR_DIV_4;  
            break;
        case 8:  
            acc_bw = sl_accelBW_ODR_DIV_8;  gyr_bw = sl_gyroBW_ODR_DIV_8;  
            break;
        case 16: 
            acc_bw = sl_accelBW_ODR_DIV_16; gyr_bw = sl_gyroBW_ODR_DIV_16; 
            break;
        default: 
            // Nếu truyền số tào lao, an toàn nhất là set mặc định chia 4
            acc_bw = sl_accelBW_ODR_DIV_4;  gyr_bw = sl_gyroBW_ODR_DIV_4;  
            break; 
    }

    // Gọi hàm cấu hình Bandwidth của hãng
    sl_icm40627_accel_set_bandwidth(acc_bw);
    sl_icm40627_gyro_set_bandwidth(gyr_bw);
    // -------------------------------------------------------------

    // 5. Bật ngắt phần cứng báo Data Ready
    sl_icm40627_enable_interrupt(true, false);

    // ... (Giữ nguyên đoạn cấu hình GPIO ngắt ở dưới) ...
    sl_gpio_t imu_int_gpio = {
        .port = SL_IMU_INT_PORT,
        .pin = SL_IMU_INT_PIN,
    };
    int32_t int_no = SL_IMU_INT_PIN;
    sl_gpio_set_pin_mode(&imu_int_gpio, SL_GPIO_MODE_INPUT, 0);
    
    status = sl_gpio_configure_external_interrupt(&imu_int_gpio, &int_no,
                                                  SL_GPIO_INTERRUPT_FALLING_EDGE,
                                                  isrCallback, this);
                                                  
    head = 0; tail = 0; count = 0;

    // Chờ 50ms cho màng cơ học khởi động hoàn toàn
    vTaskDelay(pdMS_TO_TICKS(50));

    return status;
}

void IMU::isrCallback(uint8_t int_id, void *ctx)
{
    (void) int_id;
    if (ctx != nullptr) static_cast<IMU*>(ctx)->processInterrupt();
}

void IMU::processInterrupt()
{
    // Kiểm tra cờ ngắt của chính chip ICM40627
    if (!sl_icm40627_is_data_ready()) return;

    float accel[3];
    float gyro[3];

    // Đọc Dữ Liệu Thực (Đã quy đổi ra 'g' và 'dps' cực chuẩn)
    sl_icm40627_accel_read_data(accel);
    sl_icm40627_gyro_read_data(gyro);

    // Nhân ngược lại với Scale Factor để nén thành int16_t cất vào mảng buffer
    buffer[head].x = (int16_t)(accel[0] * ACC_SCALE);
    buffer[head].y = (int16_t)(accel[1] * ACC_SCALE);
    buffer[head].z = (int16_t)(accel[2] * ACC_SCALE);
    
    buffer[head].gx = (int16_t)(gyro[0] * GYR_SCALE);
    buffer[head].gy = (int16_t)(gyro[1] * GYR_SCALE);
    buffer[head].gz = (int16_t)(gyro[2] * GYR_SCALE);

    head = (head + 1) % BUFFER_SIZE;
    if (count < BUFFER_SIZE) count++; else tail = (tail + 1) % BUFFER_SIZE;
}

int IMU::available(void) { return count; }

bool IMU::IMU_getfifo(imu_data_float_t* data_out)
{
    if (xSemaphoreTake(imuMutex, portMAX_DELAY) != pdTRUE) return false;

    CORE_DECLARE_IRQ_STATE;
    CORE_ENTER_CRITICAL();

    if (count == 0) {
        CORE_EXIT_CRITICAL();
        xSemaphoreGive(imuMutex);
        return false;
    }

    // Sử dụng Hằng số (constexpr) giúp CPU tính toán chia số cực nhanh
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
    // Khóa ngắt hệ thống (Atomic Operation)
    CORE_DECLARE_IRQ_STATE;
    CORE_ENTER_CRITICAL();

    // Dọn sạch kho chứa
    head = 0;
    tail = 0;
    count = 0;

    // Mở khóa ngắt trở lại
    CORE_EXIT_CRITICAL();
}
float normalize(float x, float mean, float std) {
    return (x - mean) / (std + 1e-8f);
}