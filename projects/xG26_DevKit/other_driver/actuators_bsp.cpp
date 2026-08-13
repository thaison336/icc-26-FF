#include "other_driver/actuators_bsp.h"

// Định nghĩa chân GPIO cho RGB LED chuẩn trên bo mạch BRD2608A Rev A04
// Thực nghiệm thực tế: PA04 là Green, PB00 là Blue, PB02 là Red (Mạch Active-Low: 0 = Sáng, 1 = Tắt)
#define RGB_RED_PORT gpioPortD
#define RGB_RED_PIN 7

#define RGB_GREEN_PORT gpioPortA
#define RGB_GREEN_PIN 4

#define RGB_BLUE_PORT gpioPortB
#define RGB_BLUE_PIN 0

#define VIB_MOTOR_PORT gpioPortB
#define VIB_MOTOR_PIN 4

#define BUZZER_PORT gpioPortB
#define BUZZER_PIN 5

static bool s_actuators_initialized = false;

void actuators_bsp_init(void)
{
    if (s_actuators_initialized)
        return;

    // Cấu hình các chân RGB LED làm Output Push-Pull (Default High = OFF cho Active Low)
    GPIO_PinModeSet(RGB_RED_PORT, RGB_RED_PIN, gpioModePushPull, 1);
    GPIO_PinModeSet(RGB_GREEN_PORT, RGB_GREEN_PIN, gpioModePushPull, 1);
    GPIO_PinModeSet(RGB_BLUE_PORT, RGB_BLUE_PIN, gpioModePushPull, 1);

    // Cấu hình chân Motor Rung và Buzzer (Default Low = OFF)
    // GPIO_PinModeSet(VIB_MOTOR_PORT, VIB_MOTOR_PIN, gpioModePushPull, 0);
    // GPIO_PinModeSet(BUZZER_PORT, BUZZER_PIN, gpioModePushPull, 0);

    s_actuators_initialized = true;
    // printf("[ACTUATORS BSP] Hardware Actuators Initialized (RGB LED, Haptic, Buzzer).\r\n");
}

void actuators_set_rgb_led(bool red, bool green, bool blue)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    // Mạch Active-Low trên BRD2608A: 0 = SÁNG, 1 = TẮT
    if (red)
        GPIO_PinOutClear(RGB_RED_PORT, RGB_RED_PIN);
    else
        GPIO_PinOutSet(RGB_RED_PORT, RGB_RED_PIN);

    if (green)
        GPIO_PinOutClear(RGB_GREEN_PORT, RGB_GREEN_PIN);
    else
        GPIO_PinOutSet(RGB_GREEN_PORT, RGB_GREEN_PIN);

    if (blue)
        GPIO_PinOutClear(RGB_BLUE_PORT, RGB_BLUE_PIN);
    else
        GPIO_PinOutSet(RGB_BLUE_PORT, RGB_BLUE_PIN);
}

void actuators_set_buzzer(bool enable)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    if (enable)
    {
        GPIO_PinOutSet(BUZZER_PORT, BUZZER_PIN);
    }
    else
    {
        GPIO_PinOutClear(BUZZER_PORT, BUZZER_PIN);
    }
}

/* =========================================================================
 * GHI ĐÈ (OVERRIDE) CÁC HÀM WEAK CỦA FSM (somniguard_fsm.cpp)
 * ========================================================================= */

// Override hàm hiển thị RGB LED theo trạng thái FSM
extern "C" void somniguard_led_display(uint8_t stateDevice)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    switch (stateDevice)
    {
    case FSM_TOP_INACTIVE:
        // Tắt hết LED khi Inactive
        actuators_set_rgb_led(false, false, false);
        break;

    case FSM_TOP_OFF_FINGER_SUSPEND:
        // Cảnh báo tuột ngón: Màu Vàng / Cam (Red + Green)
        actuators_set_rgb_led(true, true, false);
        break;

    case FSM_TOP_ACTIVE_MODE:
        // Trạng thái Active / Thức: Màu Xanh Dương (Blue)
        actuators_set_rgb_led(false, false, true);
        break;

    case FSM_TOP_NORMAL_SLEEP:
        // Trạng thái Ngủ bình thường: Màu Xanh Lá (Green)
        actuators_set_rgb_led(false, true, false);
        break;

    case FSM_TOP_DEEP_ANALYSIS:
        // Trạng thái Can thiệp Ngưng thở: Màu Đỏ (Red)
        actuators_set_rgb_led(true, false, false);
        break;

    default:
        actuators_set_rgb_led(false, false, false);
        break;
    }
}

// // Override hàm điều khiển Motor Rung (Haptic Motor)
// extern "C" void somniguard_haptic_motor(uint8_t ampHaptic, uint32_t time)
// {
//     (void)time;
//     if (!s_actuators_initialized) actuators_bsp_init();

//     if (ampHaptic > 0) {
//         GPIO_PinOutSet(VIB_MOTOR_PORT, VIB_MOTOR_PIN);
//     } else {
//         GPIO_PinOutClear(VIB_MOTOR_PORT, VIB_MOTOR_PIN);
//     }
// }
#include "../other_driver/ble_notification_manager.h"

// Override hàm BLE SOS Control
extern "C" void somniguard_BLE_control()
{
    // printf("[ACTUATORS BSP] BLE SOS Emergency Broadcast Active!\r\n");
    somniguard_ble_notify_event(
        SOMNIGUARD_BLE_EVT_TYPE_HEALTH_ALERT,
        SOMNIGUARD_BLE_EVT_CODE_APNEA_WARNING,
        0, // SpO2 param
        0  // HR param
    );
}
