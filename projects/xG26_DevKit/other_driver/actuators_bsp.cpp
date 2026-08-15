#include "other_driver/actuators_bsp.h"

// Äá»‹nh nghÄ©a chÃ¢n GPIO cho RGB LED chuáº©n trÃªn bo máº¡ch BRD2608A Rev A04
// Thá»±c nghiá»‡m thá»±c táº¿: PA04 lÃ  Green, PB00 lÃ  Blue, PB02 lÃ  Red (Máº¡ch Active-Low: 0 = SÃ¡ng, 1 = Táº¯t)
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

    // Khởi tạo PWM Hardware TIMER0 cho 3 kênh RGB LED
    actuators_pwm_init();

    s_actuators_initialized = true;
    printf("[ACTUATORS BSP] Hardware Actuators Initialized (PWM RGB LED, Haptic, Buzzer).\r\n");
}

void actuators_set_rgb_led(bool red, bool green, bool blue)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    // Đặt độ sáng PWM: true -> 255 (100%), false -> 0 (Tắt)
    actuators_set_rgb_brightness_8bit(red ? 255 : 0, green ? 255 : 0, blue ? 255 : 0);
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
 * GHI Ä Ãˆ (OVERRIDE) CÃ C HÃ€M WEAK Cá»¦A FSM (somniguard_fsm.cpp)
 * ========================================================================= */

// Override hàm hiển thị RGB LED theo trạng thái FSM bằng PWM với độ sáng dịu thích hợp
extern "C" void somniguard_led_display(uint8_t stateDevice)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    switch (stateDevice)
    {
    case FSM_TOP_INACTIVE:
        // Tắt hết LED khi Inactive
        actuators_set_rgb_brightness_8bit(0, 0, 0);
        break;

    case FSM_TOP_OFF_FINGER_SUSPEND:
        // Cảnh báo tuột ngón: Màu Vàng / Cam (Red 80 + Green 40)
        actuators_set_rgb_brightness_8bit(80, 40, 0);
        break;

    case FSM_TOP_ACTIVE_MODE:
        // Trạng thái Active / Thức: Màu Xanh Dương dịu mát (Blue 60)
        actuators_set_rgb_brightness_8bit(0, 0, 60);
        break;

    case FSM_TOP_NORMAL_SLEEP:
        // Trạng thái Ngủ đêm: Màu Xanh Lá mờ dịu mắt (Green 25 ~ 10% độ sáng)
        actuators_set_rgb_brightness_8bit(0, 25, 0);
        break;

    case FSM_TOP_DEEP_ANALYSIS:
        // Trạng thái Can thiệp Ngưng thở: Màu Đỏ báo động rực (Red 255)
        actuators_set_rgb_brightness_8bit(255, 0, 0);
        break;

    default:
        actuators_set_rgb_brightness_8bit(0, 0, 0);
        break;
    }
}
#define PWM_TOP_VALUE 255 // Tương ứng 8-bit độ sáng (0 -> 255)

void actuators_pwm_init(void)
{
    // 1. Bật xung Clock cho TIMER0 và GPIO
    CMU_ClockEnable(cmuClock_TIMER0, true);
    CMU_ClockEnable(cmuClock_GPIO, true);

    // 2. Cấu hình chân GPIO ở chế độ Push-Pull
    GPIO_PinModeSet(gpioPortD, 7, gpioModePushPull, 1); // Red
    GPIO_PinModeSet(gpioPortA, 4, gpioModePushPull, 1); // Green
    GPIO_PinModeSet(gpioPortB, 0, gpioModePushPull, 1); // Blue

    // 3. Cấu hình TIMER0 chạy ở chế độ PWM
    TIMER_Init_TypeDef timerInit = TIMER_INIT_DEFAULT;
    timerInit.prescale = timerPrescale64; // Chia tần số clock để đạt ~1kHz PWM
    timerInit.mode = timerModeUp;
    TIMER_Init(TIMER0, &timerInit);

    // Đặt TOP cho TIMER (chu kỳ PWM)
    TIMER_TopSet(TIMER0, PWM_TOP_VALUE);

    // 4. Cấu hình 3 kênh Compare (CC0, CC1, CC2) sang chế độ PWM Output
    TIMER_InitCC_TypeDef ccInit = TIMER_INITCC_DEFAULT;
    ccInit.mode = timerCCModePWM;

    TIMER_InitCC(TIMER0, 0, &ccInit); // Channel 0 -> Red
    TIMER_InitCC(TIMER0, 1, &ccInit); // Channel 1 -> Green
    TIMER_InitCC(TIMER0, 2, &ccInit); // Channel 2 -> Blue

    // 5. Route các kênh TIMER0 CC xuất ra chân GPIO thực tế của EFR32xG26
    GPIO->TIMERROUTE[0].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN | GPIO_TIMER_ROUTEEN_CC1PEN | GPIO_TIMER_ROUTEEN_CC2PEN;
    GPIO->TIMERROUTE[0].CC0ROUTE = (gpioPortD << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT) | (7 << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);
    GPIO->TIMERROUTE[0].CC1ROUTE = (gpioPortA << _GPIO_TIMER_CC1ROUTE_PORT_SHIFT) | (4 << _GPIO_TIMER_CC1ROUTE_PIN_SHIFT);
    GPIO->TIMERROUTE[0].CC2ROUTE = (gpioPortB << _GPIO_TIMER_CC2ROUTE_PORT_SHIFT) | (0 << _GPIO_TIMER_CC2ROUTE_PIN_SHIFT);

    // Mặc định cài Duty Cycle = 0 (Tắt LED Active-Low -> Compare = TOP)
    TIMER_CompareSet(TIMER0, 0, PWM_TOP_VALUE);
    TIMER_CompareSet(TIMER0, 1, PWM_TOP_VALUE);
    TIMER_CompareSet(TIMER0, 2, PWM_TOP_VALUE);

    // Cho phép TIMER0 bắt đầu chạy
    TIMER_Enable(TIMER0, true);
}

// Hàm cài đặt cường độ sáng RGB (val từ 0 đến 255)
void actuators_set_rgb_brightness_8bit(uint8_t red, uint8_t green, uint8_t blue)
{
    // Do Active-Low: CompareVal = PWM_TOP_VALUE - val
    uint32_t comp_r = PWM_TOP_VALUE - red;
    uint32_t comp_g = PWM_TOP_VALUE - green;
    uint32_t comp_b = PWM_TOP_VALUE - blue;

    TIMER_CompareBufSet(TIMER0, 0, comp_r);
    TIMER_CompareBufSet(TIMER0, 1, comp_g);
    TIMER_CompareBufSet(TIMER0, 2, comp_b);
}

// // Override hÃ m Ä‘iá»u khiá»ƒn Motor Rung (Haptic Motor)
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

// Override hÃ m BLE SOS Control
extern "C" void somniguard_BLE_control()
{
    printf("[ACTUATORS BSP] BLE SOS Emergency Broadcast Active!\r\n");
    somniguard_ble_notify_event(
        SOMNIGUARD_BLE_EVT_TYPE_HEALTH_ALERT,
        SOMNIGUARD_BLE_EVT_CODE_APNEA_WARNING,
        0, // SpO2 param
        0  // HR param
    );
}
