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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo phần cứng các ngoại vi chấp hành (RGB LED, Motor Rung, Buzzer).
 */
void actuators_bsp_init(void);

/**
 * @brief Đặt màu cho RGB LED (Active-Low trên BRD2608A).
 * @param red Bật/tắt kênh Đỏ
 * @param green Bật/tắt kênh Xanh Lá
 * @param blue Bật/tắt kênh Xanh Dương
 */
void actuators_set_rgb_led(bool red, bool green, bool blue);

/**
 * @brief Bật/tắt còi báo động (Buzzer).
 * @param enable true = Bật còi, false = Tắt còi
 */
void actuators_set_buzzer(bool enable);

#ifdef __cplusplus
}
#endif

#endif // ACTUATORS_BSP_H
