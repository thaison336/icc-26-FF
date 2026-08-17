#ifndef ACTUATORS_BSP_H
#define ACTUATORS_BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "somniguard_layer/global.h"
#include "em_gpio.h"
#include "em_cmu.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "em_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo phần cứng các ngoại vi chấp hành (Dual LED, Buzzer).
 */
void actuators_bsp_init(void);

/**
 * @brief Đặt trạng thái cho 2 LED (LED1: PB02, LED2: PC09).
 * @param led1 Bật/tắt LED1 (PB02)
 * @param led2 Bật/tắt LED2 (PC09)
 */
void actuators_set_leds(bool led1, bool led2);
void actuators_set_rgb_led(bool red, bool green, bool blue);
void actuators_pwm_init(void);
void actuators_set_rgb_brightness_8bit(uint8_t red, uint8_t green, uint8_t blue);
/**
 * @brief Bật/tắt còi báo động (Buzzer).
 * @param enable true = Bật còi, false = Tắt còi
 */
void actuators_set_buzzer(bool enable);

#ifdef __cplusplus
}
#endif

#endif // ACTUATORS_BSP_H
