#include "other_driver/actuators_bsp.h"

// Định nghĩa chân GPIO cho 2 LED trạng thái & Motor Rung
#define LED1_PORT gpioPortC
#define LED1_PIN 8 // PC08

#define LED2_PORT gpioPortC
#define LED2_PIN 9 // PC09

#define VIB_MOTOR_PORT gpioPortA
#define VIB_MOTOR_PIN 7 // PA07

#define PWM_TOP_VALUE 255 // Tương ứng 8-bit (0 -> 255)

static bool s_actuators_initialized = false;
static uint32_t s_haptic_pwm_top = PWM_TOP_VALUE;

// Flag hủy rung: set = true để abort vòng lặp haptic đang chạy ngay lập tức
static volatile bool g_haptic_abort = false;

void actuators_bsp_init(void)
{
    if (s_actuators_initialized)
        return;
    CMU_ClockEnable(cmuClock_GPIO, true);
    CMU_ClockEnable(cmuClock_TIMER0, true);
    // Cấu hình chân PA07 cho Motor Rung
    GPIO_PinModeSet(VIB_MOTOR_PORT, VIB_MOTOR_PIN, gpioModePushPull, 0);
    // 1. Cấu hình TIMER0 với Prescale 256
    TIMER_Init_TypeDef timerInit = TIMER_INIT_DEFAULT;
    timerInit.prescale = timerPrescale256; // Chia 256 để đưa clock về dải 150kHz
    timerInit.mode = timerModeUp;
    TIMER_Init(TIMER0, &timerInit);
    // 2. Tính toán TOP chính xác theo tần số clock thực tế của chip
    uint32_t timer_clk_freq = CMU_ClockFreqGet(cmuClock_TIMER0);
    uint32_t timer_tick_freq = timer_clk_freq / 256;
    s_haptic_pwm_top = (timer_tick_freq / HAPTIC_FREQ_HZ) - 1; // ~856 bước
    TIMER_TopSet(TIMER0, s_haptic_pwm_top);
    // 3. Cấu hình chế độ PWM Channel 0 trên chân PA07
    TIMER_InitCC_TypeDef ccInit = TIMER_INITCC_DEFAULT;
    ccInit.mode = timerCCModePWM;
    TIMER_InitCC(TIMER0, 0, &ccInit);
    GPIO->TIMERROUTE[0].ROUTEEN = GPIO_TIMER_ROUTEEN_CC0PEN;
    GPIO->TIMERROUTE[0].CC0ROUTE = (gpioPortA << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT) | (7 << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);
    TIMER_CompareSet(TIMER0, 0, 0); // Mặc định tắt (0% duty)
    TIMER_Enable(TIMER0, true);
    s_actuators_initialized = true;
    printf("[ACTUATORS BSP] Hardware Actuators Initialized (Dual LED: PC08, PC09 | Haptic Motor PWM: PA07).\r\n");
}

void actuators_set_leds(bool led1, bool led2)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    if (led1)
        GPIO_PinOutSet(LED1_PORT, LED1_PIN);
    else
        GPIO_PinOutClear(LED1_PORT, LED1_PIN);

    if (led2)
        GPIO_PinOutSet(LED2_PORT, LED2_PIN);
    else
        GPIO_PinOutClear(LED2_PORT, LED2_PIN);
}

void actuators_set_rgb_led(bool red, bool green, bool blue)
{
    (void)green;
    // Map tương thích: red -> LED1 (PC08), blue -> LED2 (PC09)
    actuators_set_leds(red, blue);
}

void actuators_set_haptic_pwm(uint8_t ampHaptic)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    if (ampHaptic == 0)
    {
        TIMER_CompareBufSet(TIMER0, 0, 0);
        return;
    }

    // Scale chuẩn từ dải 0..255 sang dải 0..s_haptic_pwm_top để đạt đúng 100% công suất motor
    uint32_t compare_val = ((uint32_t)ampHaptic * s_haptic_pwm_top) / 255U;
    if (compare_val > s_haptic_pwm_top)
    {
        compare_val = s_haptic_pwm_top;
    }
    TIMER_CompareBufSet(TIMER0, 0, compare_val);
}

/* =========================================================================
 * GHI ĐÈ (OVERRIDE) CÁC HÀM WEAK CỦA FSM (somniguard_fsm.cpp)
 * ========================================================================= */

// Override hàm hiển thị LED theo trạng thái FSM (Chuẩn UX Sleep Wearable: Dark-by-Default)
extern "C" void somniguard_led_display(uint8_t stateDevice)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    switch (stateDevice)
    {
    case FSM_TOP_INACTIVE:
    { // Trạng thái thức / Đeo ngón chuẩn bị ngủ: Bật LED1 (PC08) chỉ báo sẵn sàng
        static uint8_t s_boot_blink_cnt = 0;
        s_boot_blink_cnt++;
        bool is_led_on = (s_boot_blink_cnt % 2 == 1);
        actuators_set_leds(is_led_on, is_led_on);
        break;
    }
    case FSM_TOP_ACTIVE_MODE:
    {
        static uint8_t s_boot_blink_cnt = 0;
        s_boot_blink_cnt++;
        bool is_led_on = (s_boot_blink_cnt % 2 == 1);
        actuators_set_leds(is_led_on, false);
        break;
    }
    case FSM_TOP_NORMAL_SLEEP:
        // Chế độ theo dõi giấc ngủ ban đêm: TẮT HOÀN TOÀN LED để tránh gây chói và gián đoạn giấc ngủ
        actuators_set_leds(false, false);
        break;

    case FSM_TOP_DEEP_ANALYSIS:
        // Chế độ phân tích sâu / Can thiệp ngưng thở: TẮT LED (cảnh báo/kích thích chỉ dùng Haptic Motor rung & BLE)
        actuators_set_leds(false, false);
        break;

    case FSM_TOP_OFF_FINGER_SUSPEND:
        // Tuột ngón tay lúc ngủ: Tắt toàn bộ LED để không chớp sáng làm phiền người dùng trong phòng tối
        actuators_set_leds(false, false);
        break;

    default:
        // Tắt toàn bộ LED khi tắt nguồn / không hoạt động
        actuators_set_leds(false, false);
        break;
    }
}

// Override hàm điều khiển Motor Rung (Haptic Motor) qua PWM PA07
extern "C" void somniguard_haptic_motor(uint8_t ampHaptic, uint32_t time)
{
    if (ampHaptic == 0 || time == 0)
    {
        g_haptic_abort = true;       // Yêu cầu abort vòng lặp đang chạy (nếu có)
        actuators_set_haptic_pwm(0); // Tắt PWM ngay lập tức
        return;
    }

    // Bắt đầu rung mới: clear abort flag
    g_haptic_abort = false;

    // Tính chu kỳ nhịp rung dựa trên HAPTIC_BURST_RATE_HZ
    const uint32_t burst_rate = (HAPTIC_BURST_RATE_HZ == 0) ? 1 : HAPTIC_BURST_RATE_HZ;
    const uint32_t period_ms = 1000U / burst_rate;

    // Tối ưu kích thích da: Thời gian ON chiếm ~80% dồn lực quán tính, OFF 20% tạo nhịp giật dứt khoát
    const uint32_t off_ms = (period_ms >= 500) ? 150 : ((period_ms >= 200) ? 60 : (period_ms / 4));
    const uint32_t on_ms = (period_ms > off_ms) ? (period_ms - off_ms) : (period_ms / 2);

    uint32_t elapsed_ms = 0;
    while (elapsed_ms < time)
    {
        if (g_haptic_abort) // Kiểm tra abort mỗi burst cycle (~100ms)
        {
            actuators_set_haptic_pwm(0);
            return;
        }
        actuators_set_haptic_pwm(ampHaptic);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        actuators_set_haptic_pwm(0);
        vTaskDelay(pdMS_TO_TICKS(off_ms));
        elapsed_ms += period_ms;
    }
    actuators_set_haptic_pwm(0);
}

#include "ble_notification_manager.h"

// Override hàm BLE SOS Control
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

// 1. Hiệu ứng khi khởi động hệ thống trong app_init() (Chạy đuổi LED1 -> LED2)
extern "C" void somniguard_led_boot_sequence(void)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    for (int i = 0; i < 10; i++)
    {
        actuators_set_leds(true, false); // LED1 ON
        vTaskDelay(pdMS_TO_TICKS(120));
        actuators_set_leds(false, true); // LED2 ON
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    actuators_set_leds(false, false);
}

// 2. Báo hiệu khởi tạo hệ thống & Task thành công (Chớp 2 LED đồng thời 2 lần)
extern "C" void somniguard_led_boot_success(void)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    for (int i = 0; i < 10; i++)
    {
        actuators_set_leds(true, true);
        vTaskDelay(pdMS_TO_TICKS(150));
        actuators_set_leds(false, false);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

// 3. Báo hiệu bắt đầu vào chế độ theo dõi giấc ngủ ban đêm (Nháy dịu LED2 3 lần rồi TẮT HẲN)
extern "C" void somniguard_led_sleep_buffering_start(void)
{
    if (!s_actuators_initialized)
        actuators_bsp_init();

    for (int i = 0; i < 3; i++)
    {
        actuators_set_leds(false, true); // Bật LED2 (PC09)
        vTaskDelay(pdMS_TO_TICKS(200));
        actuators_set_leds(false, false); // Tắt
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    actuators_set_leds(false, false); // Đảm bảo tắt hẳn (Dark-by-Default)
}
