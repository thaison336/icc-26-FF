/**
 * @file battery_monitor.cpp
 * @brief SomniGuard Battery Monitor — Trình điều khiển IADC0 đọc điện áp pin qua chân chuyên dụng AIN0 (Breakout Pad 1)
 */

#include "battery_monitor.h"
#include "em_cmu.h"
#include "em_device.h"
#include "em_gpio.h"
#include "em_iadc.h"
#include <cstdio>
#include <stddef.h>

/* Bảng tra cứu đường cong xả phi tuyến pin LiPo (mV -> % dung lượng) */
typedef struct
{
  uint16_t voltage_mv;
  uint8_t percentage;
} lipo_lut_entry_t;

static const lipo_lut_entry_t LIPO_DISCHARGE_TABLE[] = {
    {4180, 100}, {4050, 90}, {3950, 80}, {3850, 70}, {3800, 60}, {3750, 50}, {3700, 30}, {3650, 20}, // Ngưỡng cảnh báo pin yếu
    {3550, 10},
    {3400, 5}, // Ngưỡng nguy cấp
    {3100, 0}  // Cạn pin hoàn toàn
};

#define LIPO_TABLE_SIZE \
  (sizeof(LIPO_DISCHARGE_TABLE) / sizeof(LIPO_DISCHARGE_TABLE[0]))

/* Bộ đệm lọc trung bình trượt */
static uint32_t s_sample_history[BATT_FILTER_SAMPLES] = {0};
static uint8_t s_history_idx = 0;
static uint8_t s_sample_count = 0;
static uint32_t s_current_vbat_mv = 4000; // Mặc định ban đầu 4.0V
static uint32_t s_latest_raw_code = 0;
static bool s_initialized = false;

void battery_monitor_init(void)
{
  if (s_initialized)
    return;

  // 1. Cấp clock cho IADC0 an toàn
  CMU_ClockEnable(cmuClock_IADC0, true);
  CMU_ClockSelectSet(cmuClock_IADCCLK, cmuSelect_FSRCO); // FSRCO = 20 MHz

  // 2. Reset và cấu hình IADC0
  IADC_reset(IADC0);

  IADC_Init_t init = IADC_INIT_DEFAULT;
  init.warmup = iadcWarmupNormal;                                               // Giữ ADC core và tham chiếu nội 1.21V luôn sẵn sàng, ngăn sụt áp Vref về VDDX
  init.srcClkPrescale = IADC_calcSrcClkPrescale(IADC0, 20000000UL, 20000000UL); // 0 (1:1 với FSRCO 20MHz)
  init.timebase = IADC_calcTimebase(IADC0, 20000000UL);                         // Chuẩn hóa 1us theo FSRCO 20MHz (timebase = 19)

  IADC_AllConfigs_t allConfigs = IADC_ALLCONFIGS_DEFAULT;

  allConfigs.configs[0].reference = iadcCfgReferenceInt1V2;       // Tham chiếu nội 1.21V
  allConfigs.configs[0].analogGain = iadcCfgAnalogGain0P5x;       // Gain 0.5x -> Full-scale input = 1.21V / 0.5 = 2.42V
  allConfigs.configs[0].twosComplement = iadcCfgTwosCompUnipolar; // Ép chế độ Unipolar (0 - 4095)
  allConfigs.configs[0].vRef = 1210;
  allConfigs.configs[0].adcClkPrescale = IADC_calcAdcClkPrescale(
      IADC0, 10000000UL, 20000000UL, iadcCfgModeNormal, init.srcClkPrescale);

  IADC_InitSingle_t initSingle = IADC_INITSINGLE_DEFAULT;

  IADC_SingleInput_t input = IADC_SINGLEINPUT_DEFAULT;
  input.posInput = iadcPosInputPadAna0; // AIN0 (Chân chuyên dụng Analog Pad 1 trên Breakout Header bên trái)
  input.negInput = iadcNegInputGnd;
  input.configId = 0;

  IADC_init(IADC0, &init, &allConfigs); // Tự handle SYNCBUSY bên trong
  IADC_initSingle(IADC0, &initSingle, &input);

  s_initialized = true;
  battery_monitor_sample();
  printf("[BATT] Battery monitor initialized on AIN0 (Pad 1) (KeepWarm, Vref 1.21V, Gain 0.5x)!\r\n");
}

void battery_monitor_sample(void)
{
  if (!s_initialized)
  {
    battery_monitor_init();
  }

  // Xóa cờ ngắt hoàn thành trước khi trigger
  IADC0->IF_CLR = IADC_IF_SINGLEDONE;

  // Kích hoạt 1 lần chuyển đổi Single Conversion
  IADC0->CMD = IADC_CMD_SINGLESTART;

  // Chờ quá trình chuyển đổi hoàn tất
  uint32_t timeout = 50000;
  while (!(IADC0->IF & IADC_IF_SINGLEDONE) && (--timeout > 0))
  {
    // Chờ kết quả
  }

  if (timeout == 0)
  {
    printf("[BATT ERROR] IADC conversion timeout!\r\n");
    return; // Quá thời gian chờ (timeout error)
  }

  // Đọc kết quả từ Single FIFO bằng API của SDK
  IADC_Result_t res = IADC_pullSingleFifoResult(IADC0);
  uint32_t raw_code = res.data;
  s_latest_raw_code = raw_code;

  // Tính điện áp đo được tại chân AIN0 (mV)
  // Vadc = raw_code * Vref_full_scale / Resolution
  uint32_t vadc_mv = (raw_code * BATT_VREF_MV) / BATT_ADC_RESOLUTION;

  // Nhân với tỉ lệ phân áp R1/R2 để ra điện áp pin thực tế: Vbat = Vadc * 2.0
  uint32_t vbat_sample_mv = (uint32_t)(vadc_mv * BATT_DIVIDER_RATIO);

  if (vbat_sample_mv < BATT_VOLTAGE_DISCONNECTED_MV)
  {
    printf("[BATT DEBUG] Raw: %4lu | Vadc: %4lu mV | Vbat: %4lu mV [CFG: 0x%08lx] (CHUA NOI PIN / FLOATING)\r\n",
           raw_code, vadc_mv, vbat_sample_mv, IADC0->CFG[0].CFG);
    printf("CFG0: 0x%08lx | CFG1: 0x%08lx | SINGLE: 0x%08lx\r\n",
           IADC0->CFG[0].CFG, IADC0->CFG[1].CFG, IADC0->SINGLE);
  }
  else
  {
    printf("[BATT DEBUG] Raw: %4lu | Vadc: %4lu mV | Vbat: %4lu mV | Batt: %3u%% [CFG: 0x%08lx]\r\n",
           raw_code, vadc_mv, vbat_sample_mv, battery_monitor_get_percent(), IADC0->CFG[0].CFG);
    printf("CFG0: 0x%08lx | CFG1: 0x%08lx | SINGLE: 0x%08lx\r\n",
           IADC0->CFG[0].CFG, IADC0->CFG[1].CFG, IADC0->SINGLE);
  }

  // Đưa mẫu vào bộ đệm trung bình trượt
  s_sample_history[s_history_idx] = vbat_sample_mv;
  s_history_idx = (s_history_idx + 1) % BATT_FILTER_SAMPLES;
  if (s_sample_count < BATT_FILTER_SAMPLES)
  {
    s_sample_count++;
  }

  // Tính giá trị trung bình trượt
  uint32_t sum = 0;
  for (uint8_t i = 0; i < s_sample_count; i++)
  {
    sum += s_sample_history[i];
  }
  s_current_vbat_mv = sum / s_sample_count;
}

uint32_t battery_monitor_get_raw(void) { return s_latest_raw_code; }

uint32_t battery_monitor_get_voltage_mv(void) { return s_current_vbat_mv; }

uint8_t battery_monitor_get_percent(void)
{
  uint32_t mv = s_current_vbat_mv;

  // Nếu chân hở hoặc chưa cắm pin (< 2.0V)
  if (mv < BATT_VOLTAGE_DISCONNECTED_MV)
  {
    return 0;
  }

  // Nếu pin đầy hơn ngưỡng tối đa
  if (mv >= LIPO_DISCHARGE_TABLE[0].voltage_mv)
  {
    return 100;
  }

  // Nếu pin cạn hơn ngưỡng tối thiểu
  if (mv <= LIPO_DISCHARGE_TABLE[LIPO_TABLE_SIZE - 1].voltage_mv)
  {
    return 0;
  }

  // Nội suy tuyến tính theo từng đoạn (Piecewise Linear Interpolation)
  for (size_t i = 0; i < LIPO_TABLE_SIZE - 1; i++)
  {
    if (mv <= LIPO_DISCHARGE_TABLE[i].voltage_mv &&
        mv >= LIPO_DISCHARGE_TABLE[i + 1].voltage_mv)
    {
      uint32_t v_high = LIPO_DISCHARGE_TABLE[i].voltage_mv;
      uint32_t v_low = LIPO_DISCHARGE_TABLE[i + 1].voltage_mv;
      uint32_t p_high = LIPO_DISCHARGE_TABLE[i].percentage;
      uint32_t p_low = LIPO_DISCHARGE_TABLE[i + 1].percentage;

      return (uint8_t)(p_low +
                       ((mv - v_low) * (p_high - p_low)) / (v_high - v_low));
    }
  }

  return 50;
}

bool battery_monitor_is_low(void)
{
  return (s_current_vbat_mv <= BATT_VOLTAGE_LOW_MV);
}

bool battery_monitor_is_critical(void)
{
  return (s_current_vbat_mv <= BATT_VOLTAGE_CRIT_MV);
}

bool battery_monitor_is_connected(void)
{
  return (s_current_vbat_mv >= BATT_VOLTAGE_DISCONNECTED_MV);
}
