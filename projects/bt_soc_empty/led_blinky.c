/***************************************************************************//**
 * @file led_blinky.c
 * @brief Implementation for LED Blinky module.
 ******************************************************************************/
#include "led_blinky.h"
#include "sl_gpio.h"
#include "pin_config.h"
#include "app_assert.h"

// Biến cấu hình chân LED từ pin_config.h
static const sl_gpio_t led_gpio = { .port = LED_PORT, .pin = LED_PIN };

void led_blinky_init(void)
{
  // Khởi tạo chân LED làm Output Push-Pull, mặc định LOW (tắt)
  sl_gpio_set_pin_mode(&led_gpio, SL_GPIO_MODE_PUSH_PULL, false);
}

void led_blinky_start(uint32_t interval_ms)
{
  // 1. Bật LED (HIGH) ngay lập tức
  sl_gpio_set_pin(&led_gpio);

  // 2. Tính số ticks: interval_ms * 32.768 ticks/ms = (interval_ms * 32768) / 1000
  uint32_t ticks = (interval_ms * 32768) / 1000;

  // 3. Kích hoạt soft timer lặp liên tục (single_shot = 0)
  sl_status_t sc = sl_bt_system_set_lazy_soft_timer(ticks, 0, TIMER_LED_BLINK_ID, 0);
  app_assert_status(sc);
}

void led_blinky_stop(void)
{
  // 1. Dừng soft timer (truyền time = 0)
  sl_bt_system_set_lazy_soft_timer(0, 0, TIMER_LED_BLINK_ID, 0);

  // 2. Tắt LED
  sl_gpio_clear_pin(&led_gpio);
}

void led_blinky_on_event(sl_bt_msg_t *evt)
{
  if (SL_BT_MSG_ID(evt->header) == sl_bt_evt_system_soft_timer_id) {
    if (evt->data.evt_system_soft_timer.handle == TIMER_LED_BLINK_ID) {
      // Đảo trạng thái LED liên tục mỗi lần Soft Timer kích hoạt
      sl_gpio_toggle_pin(&led_gpio);
    }
  }
}
