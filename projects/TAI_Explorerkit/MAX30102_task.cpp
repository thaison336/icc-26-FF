#include "sensor_task.h"
#include "MAX30102_driver/MAX30105.h" // Thư viện cảm biến bạn đã port
#include "task.h"

// 1. Định nghĩa thực thể của các biến toàn cục
QueueHandle_t xSensorQueue = NULL;
MAX30105 particleSensor; // Đối tượng C++ của cảm biến

// 2. Khai báo hàm Task nội bộ (dùng 'static' để ẩn hàm này khỏi các file khác)
static void vSensorTask(void *pvParameters) {
    // ==========================================
    // PHẦN KHỞI TẠO CẢM BIẾN
    // ==========================================
    
    // Khởi tạo I2C ở tốc độ 100kHz
    if (!particleSensor.begin(100000)) {
        // Treo Task nếu không tìm thấy cảm biến
        vTaskSuspend(NULL); 
    }

    // Cấu hình: SpO2 Mode (Red + IR), 100Hz, 18-bit resolution, 6.4mA LED
    particleSensor.setup(0x1F, 4, 2, 100, 411, 4096);

    sensor_data_t new_sample;
    
    // Cài đặt chu kỳ thời gian 10ms (100Hz)
    const TickType_t xFrequency = pdMS_TO_TICKS(10); 
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // ==========================================
    // VÒNG LẶP CHÍNH CỦA TASK
    // ==========================================
    while (1) {
        // Đọc toàn bộ dữ liệu mới từ phần cứng vào RAM
        particleSensor.check();

        // Rút từng mẫu dữ liệu ra và gửi đi
        while (particleSensor.available()) {
            new_sample.red_raw = particleSensor.getFIFORed();
            new_sample.ir_raw  = particleSensor.getFIFOIR();

            // Đẩy vào Queue (Timeout = 0 để không bị block nếu Queue đầy)
            xQueueSend(xSensorQueue, &new_sample, 0);

            particleSensor.nextSample();
        }

        // Nhường CPU cho đến chu kỳ 10ms tiếp theo
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// 3. Triển khai hàm khởi tạo (Được gọi từ main hoặc app_init)
void app_sensor_task_init(void) {
    // Tạo Queue chứa được tối đa 500 mẫu
    xSensorQueue = xQueueCreate(500, sizeof(sensor_data_t));
    
    if (xSensorQueue != NULL) {
        // Tạo FreeRTOS Task
        xTaskCreate(vSensorTask, 
                    "Sensor Task", 
                    256,          // Kích thước Stack 
                    NULL, 
                    3,            // Mức ưu tiên (Priority)
                    NULL);
    }
}