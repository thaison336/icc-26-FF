#ifndef MAX30102_H
#define MAX30102_H
#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#ifdef __cplusplus
extern "C" {
#endif
// 1. Định nghĩa cấu trúc gói dữ liệu thô
typedef struct {
    uint32_t red_raw;
    uint32_t ir_raw;
} sensor_data_t;

// 2. Khai báo extern cho Queue
// Dùng 'extern' để báo cho trình biên dịch biết rằng biến xSensorQueue 
// đã được tạo ra ở một nơi khác (trong file .cpp), các file khác cứ lấy mà dùng.
extern QueueHandle_t xSensorQueue;

// 3. Khai báo nguyên mẫu hàm (Prototype)
// Các file khác chỉ cần gọi hàm này lúc khởi động hệ thống
void app_sensor_task_init(void);
#ifdef __cplusplus
}
#endif
#endif // MAX30102_H