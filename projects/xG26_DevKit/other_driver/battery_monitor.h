#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

/**
 * @file battery_monitor.h
 * @brief SomniGuard Battery Monitor — Đo điện áp pin LiPo qua IADC0 trên chân PD02 (Chân 20 Header P3)
 *
 * Sơ đồ mạch phân áp:
 *   VBAT (3.0V – 4.2V)
 *       │
 *     [R1 = 1MΩ]
 *       │
 *       ├──► PD02 (Chân 20 Header P3 trên BRD2709A - MIKROE_ANALOG)
 *       │
 *     [R2 = 1MΩ]
 *       │
 *      GND (Chân 5 hoặc 25 Header P2 trên BRD2709A)
 *
 *   Vout = Vbat × R2 / (R1 + R2) = Vbat / 2.0
 *   → Dải điện áp đưa vào PD02: 1.50V – 2.10V (nằm an toàn trong dải 0 - 2.5V)
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

/** @brief Cổng và chân GPIO nối vào cầu phân áp (Chân 20 Header P3) */
#define BATT_ADC_PORT gpioPortD
#define BATT_ADC_PIN 2U // PD02

/** @brief Tỉ lệ cầu phân áp: (R1 + R2) / R2 = (1M + 1M) / 1M = 2.0 */
#define BATT_DIVIDER_RATIO 2.0f

/** @brief Điện áp toàn thang đo IADC0 tại chân GPIO (mV):
 *  Tham chiếu nội 1.21V (iadcCfgReferenceInt1V2) với Gain 0.5x (iadcCfgAnalogGain0P5x)
 *  => Dải đo tối đa tại chân PD02: 1210 mV / 0.5 = 2420 mV
 */
#define BATT_VREF_MV 2420U

/** @brief Độ phân giải IADC0 12-bit (0 - 4095) */
#define BATT_ADC_RESOLUTION 4096U

/** @brief Số mẫu của bộ lọc trung bình trượt */
#define BATT_FILTER_SAMPLES 8U

/* =========================================================================
 * NGƯỠNG ĐIỆN ÁP PIN LiPo (mV)
 * ========================================================================= */
#define BATT_VOLTAGE_FULL_MV 4180U  // 100%
#define BATT_VOLTAGE_EMPTY_MV 3100U // 0% (Cutoff)
#define BATT_VOLTAGE_LOW_MV 3650U   // Ngưỡng cảnh báo pin yếu (~20%)
#define BATT_VOLTAGE_CRIT_MV 3400U  // Ngưỡng pin nguy cấp (~5%)

    /* =========================================================================
     * API ĐIỀU KHIỂN
     * ========================================================================= */

    /**
     * @brief Khởi tạo ngoại vi IADC0 và cấu hình chân PD02 sang chế độ Analog
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

#ifdef __cplusplus
}
#endif

#endif // BATTERY_MONITOR_H
