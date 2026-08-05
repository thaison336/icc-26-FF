/***************************************************************************//**
 * @file led_blinky.h
 * @brief Header file for LED Blinky module.
 ******************************************************************************/
#ifndef LED_BLINKY_H
#define LED_BLINKY_H

#include "sl_bt_api.h"

// ID của Soft Timer dành riêng cho LED Blinky module
#define TIMER_LED_BLINK_ID   0

/**
 * @brief Khởi tạo module LED Blinky (cấu hình GPIO pin).
 */
void led_blinky_init(void);

/**
 * @brief Bắt đầu cho LED chớp nháy liên tục theo chu kỳ (tính bằng ms).
 * @param[in] interval_ms Thời gian chớp tắt (ví dụ: 500ms)
 */
void led_blinky_start(uint32_t interval_ms);

/**
 * @brief Dừng chớp nháy LED và tắt LED.
 */
void led_blinky_stop(void);

/**
 * @brief Xử lý các sự kiện BLE liên quan tới LED Blinky.
 * @param[in] evt Con trỏ sự kiện từ BLE Stack.
 */
void led_blinky_on_event(sl_bt_msg_t *evt);

#endif // LED_BLINKY_H
