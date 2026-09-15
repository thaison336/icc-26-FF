#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

/**
 * @file battery_monitor.h
 * @brief SomniGuard Battery Monitor — Đo điện áp pin LiPo qua IADC0 trên chân chuyên dụng AIN0 (Breakout Pad 1 trên kit BRD2709A)
 *
 * Sơ đồ mạch phân áp:
 *   VBAT (3.0V – 4.2V)
 *       │
 *     [R1 = 47kΩ]
 *       │
 *       ├──► AIN0 (Chân số 1 - Hàng chân bên trái của kit BRD2709A)
 *       │
 *     [R2 = 47kΩ] (kèm tụ 10nF song song với R2 xuống GND)
 *       │
 *      GND (Chân GND - Pad số 5 ngay trên cùng hàng chân bên trái)
 *
 *   Vout = Vbat × R2 / (R1 + R2) = Vbat / 2.0
 *   → Dải điện áp đưa vào AIN0: 1.50V – 2.10V (nằm an toàn trong dải 0 - 2.42V)
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* =========================================================================
 * CẤU HÌNH PHẦN CỨNG
 * ========================================================================= */

/** @brief Ngõ vào IADC: AIN0 (Chân Analog chuyên dụng Pad 1 trên hàng chân bên trái) */
#define BATT_ADC_PAD_AIN0 1

/** @brief Tỉ lệ cầu phân áp: (R1 + R2) / R2 = (47k + 47k) / 47k = 2.0 */
#define BATT_DIVIDER_RATIO 2.0f

/** @brief Điện áp toàn thang đo IADC0 tại chân AIN0 (mV):
 *  Vref nội = 1.21V (1210 mV), Gain = 0.5x -> Dải đo Full-scale = 1210 / 0.5 = 2420 mV.
 *  Với áp tại chân AIN0 là 1.60V (1600 mV):
 *  Mã Raw chuẩn = (1600 / 2420) * 4096 = 2708.
 */
#define BATT_VREF_MV 2420U

/** @brief Độ phân giải IADC0 12-bit (0 - 4095) */
#define BATT_ADC_RESOLUTION 4096U

/** @brief Số mẫu của bộ lọc trung bình trượt */
#define BATT_FILTER_SAMPLES 8U

/* =========================================================================
 * NGƯỠNG ĐIỆN ÁP PIN LiPo (mV)
 * ========================================================================= */
#define BATT_VOLTAGE_FULL_MV 4180U         // 100%
#define BATT_VOLTAGE_EMPTY_MV 3100U        // 0% (Cutoff)
#define BATT_VOLTAGE_LOW_MV 3650U          // Ngưỡng cảnh báo pin yếu (~20%)
#define BATT_VOLTAGE_CRIT_MV 3400U         // Ngưỡng pin nguy cấp (~5%)
#define BATT_VOLTAGE_DISCONNECTED_MV 2000U // Ngưỡng ngắt kết nối: < 2.0V coi như hở mạch / chưa cắm pin

    /* =========================================================================
     * API ĐIỀU KHIỂN
     * ========================================================================= */

    /**
     * @brief Khởi tạo ngoại vi IADC0 và ngõ vào chuyên dụng AIN0 (Breakout Pad 1)
     */
    void battery_monitor_init(void);

    /**
     * @brief Thực hiện một lần đo ADC và cập nhật bộ đệm lọc trung bình trượt
     */
    void battery_monitor_sample(void);

    /**
     * @brief Lấy giá trị mã ADC raw (0 - 4095) của lần lấy mẫu gần nhất (phục vụ debug)
     * @return Giá trị ADC thô 12-bit
     */
    uint32_t battery_monitor_get_raw(void);

    /**
     * @brief Lấy điện áp pin LiPo đã qua lọc (mV)
     * @return Điện áp pin tính bằng milliVolt (VD: 3820 = 3.82V)
     */
    uint32_t battery_monitor_get_voltage_mv(void);

    /**
     * @brief Lấy phần trăm pin theo bảng tra xả phi tuyến LiPo
     * @return 0 - 100 (%)
     */
    uint8_t battery_monitor_get_percent(void);

    /**
     * @brief Kiểm tra pin có đang ở mức yếu cần sạc không (< 20%)
     */
    bool battery_monitor_is_low(void);

    /**
     * @brief Kiểm tra pin có đang ở mức nguy cấp không (< 5%)
     */
    bool battery_monitor_is_critical(void);

    /**
     * @brief Kiểm tra pin có đang được kết nối thực sự hay chân AIN0 đang hở/chưa cắm pin
     * @return true nếu Vbat >= 2000mV, false nếu chân hở/floating
     */
    bool battery_monitor_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_MONITOR_H
