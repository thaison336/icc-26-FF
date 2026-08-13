// /***************************************************************************//**
//  * @file
//  * @brief Top level application functions
//  *******************************************************************************
//  * # License
//  * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
//  *******************************************************************************
//  *
//  * The licensor of this software is Silicon Laboratories Inc. Your use of this
//  * software is governed by the terms of Silicon Labs Master Software License
//  * Agreement (MSLA) available at
//  * www.silabs.com/about-us/legal/master-software-license-agreement. This
//  * software is distributed to you in Source Code format and is governed by the
//  * sections of the MSLA applicable to Source Code.
//  *
//  ******************************************************************************/

// /***************************************************************************//**
//  * Initialize application.
//  ******************************************************************************/

#include "sl_iostream.h"
#include "stdio.h"
// #include "MAX30102_task.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "MAX30102_driver/MAX30105.h"
#include "sl_simple_led_instances.h"
#include "em_gpio.h"
#include "em_i2c.h"

#ifndef LED_INSTANCE
#define LED_INSTANCE    sl_led_led0
#endif
// static MAX30105 testSensor;
// static I2CBus myI2CBus(I2C1);


// static void vSensorReadTask(void *pvParameters)
// {
//     MAX30105 *sensor = static_cast<MAX30105 *>(pvParameters);

//     // Cấu hình MAX30102
//     sensor->setup(
//         0x1F,   // LED current
//         4,      // Sample average
//         2,      // RED + IR
//         100,    // Sample rate
//         411,    // Pulse width
//         4096);  // ADC range
//         sensor->enableFIFORollover();

//     while (1)
// {
// uint8_t wp0 = sensor->getWritePointer();

// TickType_t t0 = xTaskGetTickCount();

// vTaskDelay(pdMS_TO_TICKS(1000));

// uint8_t wp1 = sensor->getWritePointer();

// TickType_t t1 = xTaskGetTickCount();

// printf("Ticks = %lu\n", (unsigned long)(t1 - t0));
// printf("WP0=%u WP1=%u Diff=%u\n",
//        wp0,
//        wp1,
//        (wp1 - wp0 + 32) % 32);
// }
// }
#include "sl_simple_led_instances.h" // Chứa sl_led_led0 (nếu dùng component Simple LED)
#include "em_gpio.h"

#include "model/model.h"
#include <string.h>
#include <stdlib.h>

extern "C" {
    void HardFault_Handler(void) {
        // Bật LED0 lên để báo hiệu HardFault (nếu printf không chạy được trong ngắt)
        sl_led_turn_on(&sl_led_led0);
        printf("HARDFAULT\r\n");
        while(1);
    }
    
    void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
        printf("STACK_OVERFLOW: %s\r\n", pcTaskName);
        while(1);
    }
    
    void vApplicationMallocFailedHook(void) {
        printf("MALLOC_FAILED\r\n");
        while(1);
    }
}

// Task chính xử lý Serial và AI
static void main_app_task(void *pvParameters)
{
    (void)pvParameters;
    char line_buf[512];
    int idx = 0;

    while (1) {
        // Đọc 1 ký tự từ Serial
        int c = getchar(); 
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(1)); // Nhường CPU nếu không có ký tự
            continue;
        }

        if (c == '\n' || c == '\r') {
            if (idx > 0) {
                line_buf[idx] = '\0';
                
                if (strncmp(line_buf, "RESET", 5) == 0) {
                    reset_buffer();
                    fflush(stdout);
                } else if (line_buf[0] == 'W' && line_buf[1] == ',') {
                    float frame[28];
                    int count = 0;
                    char* p = line_buf + 2;
                    while (p && *p && count < 28) {
                        char* next_comma = strchr(p, ',');
                        if (next_comma) *next_comma = '\0';
                        frame[count++] = atof(p);
                        if (next_comma) p = next_comma + 1;
                        else break;
                    }
                    if (count == 28) {
                        process_new_frame(frame);
                        fflush(stdout);
                    } else {
                        printf("ERR:BadFrame:%d\r\n", count);
                        fflush(stdout);
                    }
                }
                idx = 0; // Reset buffer
            }
        } else {
            if (idx < sizeof(line_buf) - 1) {
                line_buf[idx++] = c;
            }
        }
    }
}

void app_init(void)
{
    printf("========== APP INIT START ==========\r\n");
    
    // Khởi tạo model TFLM
    printf("\r\n--- Initializing Model ---\r\n");
    init_model();
    printf("READY\r\n");
    fflush(stdout);

    // Tạo Task để hệ điều hành quản lý thay vì block ở bare-metal
    xTaskCreate(main_app_task, "AppTask", 2048, NULL, 1, NULL);
}

/***************************************************************************//**
 * App ticking function.
 ******************************************************************************/
void app_process_action(void)
{
}




// #include "app.h"
// #include "FreeRTOS.h"
// #include "task.h"
// #include "em_gpio.h"

// // Task chớp đèn độc lập
// static void vBlinkTask(void *pvParameters) {
//     // Đảm bảo đúng chân PC08
//     GPIO_PinModeSet(gpioPortC, 8, gpioModePushPull, 1);
    
//     while (1) {
//         GPIO_PinOutToggle(gpioPortC, 8); // Đảo trạng thái LED
//         vTaskDelay(pdMS_TO_TICKS(250));  // Chờ nửa giây
//     }
// }

// void app_init(void) {
//     // Tạo Task và đưa vào hàng đợi trước khi hệ điều hành thức giấc
//     xTaskCreate(vBlinkTask, "Blink", 512, NULL, 1, NULL);
// }

// void app_process_action(void) {
//     // Bỏ trống
// }