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
 * @brief Khởi tạo phần cứng các ngoại vi chấp hành (Dual LED: PC08, PC09 | Haptic PWM: PA07).
 */
void actuators_bsp_init(void);

/**
 * @brief Đặt trạng thái cho 2 LED (LED1: PC08, LED2: PC09).
 * @param led1 Bật/tắt LED1 (PC08)
 * @param led2 Bật/tắt LED2 (PC09)
 */
void actuators_set_leds(bool led1, bool led2);
void actuators_set_rgb_led(bool red, bool green, bool blue);
void actuators_set_haptic_pwm(uint8_t ampHaptic);

/**
 * @brief Các hiệu ứng hiển thị LED cho Booting & Setup các Mode FSM.
 */
void somniguard_led_boot_sequence(void);
void somniguard_led_boot_success(void);
void somniguard_led_sleep_buffering_start(void);

#ifdef __cplusplus
}
#endif

#endif // ACTUATORS_BSP_H

