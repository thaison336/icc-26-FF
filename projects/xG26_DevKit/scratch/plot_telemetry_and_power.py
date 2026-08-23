#!/usr/bin/env python3
"""
plot_telemetry_and_power.py

Script phân tích tự động file log telemetry thực tế (somniguard_telemetry_20260814_134505_1.csv),
tính toán chi tiết Power Budget và vẽ đồ thị báo cáo cho Slide 'Power Budget & Impact'.
"""

import pandas as pd
import numpy as np
import re
import sys
import matplotlib.pyplot as plt

sys.stdout.reconfigure(encoding='utf-8')

def analyze_and_plot(csv_filepath="somniguard_telemetry_20260814_134505_1.csv"):
    print(f"Đang đọc dữ liệu từ {csv_filepath}...")
    df = pd.read_csv(csv_filepath)

    # 1. Tính toán thời gian
    ts_min = df['Timestamp'].min()
    ts_max = df['Timestamp'].max()
    duration_hours = (ts_max - ts_min) / (1000.0 * 3600.0)

    # 2. Bóc tách SpO2 & BPM
    records = []
    for idx, row in df.iterrows():
        p = str(row.get('RawPayload', ''))
        m_spo2 = re.search(r'SpO2:([\d\.]+)%', p)
        m_bpm = re.search(r'BPM:(\d+)', p)
        if m_spo2:
            spo2_val = float(m_spo2.group(1))
            bpm_val = float(m_bpm.group(1)) if m_bpm else 75.0
            time_rel_min = (row['Timestamp'] - ts_min) / (1000.0 * 60.0)
            records.append({
                'Time_min': time_rel_min,
                'SpO2': spo2_val,
                'BPM': bpm_val
            })

    telemetry_df = pd.DataFrame(records)
    print(f"-> Đã bóc tách thành công {len(telemetry_df):,} gói tin SpO2/BPM hợp lệ.")
    print(f"-> Tổng thời gian chạy thử nghiệm: {duration_hours:.2f} giờ ({duration_hours*60:.1f} phút).")

    # 3. Tính toán Power Budget theo thông số chip Silicon Labs EFR32xG26
    v_supply = 3.3 # Volts
    i_mcu = 0.91   # mA (EM0 78MHz + MVP AI 3% duty + EM2 deep sleep 97% + I2C/DMA)
    i_ppg = 1.60   # mA (MAX30102 50Hz, pulsed LEDs)
    i_imu = 0.45   # mA (MPU6050 50Hz low-power mode)
    i_ble = 0.28   # mA (BLE 5.4 1s interval + notifications)
    i_haptic = 1.60# mA (Haptic burst duty ~5.3% trong các cơn ngưng thở)

    i_total = i_mcu + i_ppg + i_imu + i_ble + i_haptic
    power_mw = i_total * v_supply

    battery_150mah_hours = (150.0 * 0.85) / i_total
    battery_300mah_hours = (300.0 * 0.85) / i_total

    print("\n========== KẾT QUẢ TÍNH TOÁN POWER BUDGET ==========")
    print(f"Dòng điện trung bình toàn hệ thống: {i_total:.2f} mA")
    print(f"Công suất tiêu thụ trung bình     : {power_mw:.2f} mW")
    print(f"Thời lượng pin 150mAh LiPo         : {battery_150mah_hours:.1f} Giờ (> 2.6 đêm ngủ 8-10h)")
    print(f"Thời lượng pin 300mAh LiPo         : {battery_300mah_hours:.1f} Giờ (> 5 đêm ngủ liên tục)")

    # 4. Xuất biểu đồ phân bổ năng lượng & xu hướng SpO2
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

    # Biểu đồ tròn phân bổ dòng tiêu thụ
    labels = ['EFR32xG26 MCU\n(EM0/EM2/MVP)', 'MAX30102 PPG\n(50Hz Red/IR)', 'MPU6050 IMU\n(50Hz)', 'BLE 5.4 Radio\n(Telemetry)', 'Haptic Motor\n(Intervention)']
    currents = [i_mcu, i_ppg, i_imu, i_ble, i_haptic]
    colors = ['#8b5cf6', '#ef4444', '#3b82f6', '#10b981', '#f59e0b']

    ax1.pie(currents, labels=labels, autopct='%1.1f%%', startangle=140, colors=colors, explode=(0.05, 0.05, 0, 0, 0.05))
    ax1.set_title(f'Hardware Power Budget Breakdown (Avg: {i_total:.2f} mA @ 3.3V)', fontsize=12, fontweight='bold')

    # Biểu đồ SpO2 theo thời gian thực nghiệm
    sample_sub = telemetry_df.iloc[::10] # Lấy mẫu hiển thị mượt
    ax2.plot(sample_sub['Time_min'], sample_sub['SpO2'], color='#ef4444', linewidth=1, label='SpO2 (%)')
    ax2.axhline(y=90.0, color='darkred', linestyle='--', label='Hypoxia Threshold (90%)')
    ax2.set_xlabel('Elapsed Time (Minutes)', fontsize=11)
    ax2.set_ylabel('SpO2 Level (%)', fontsize=11)
    ax2.set_title(f'Overnight Telemetry Simulation ({duration_hours:.1f} Hours Monitoring)', fontsize=12, fontweight='bold')
    ax2.set_ylim(60, 100)
    ax2.grid(True, linestyle=':', alpha=0.6)
    ax2.legend(loc='lower left')

    plt.tight_layout()
    chart_path = 'specDocument/image/power_budget_telemetry_chart.png'
    plt.savefig(chart_path, dpi=200)
    print(f"\n-> Đã lưu biểu đồ vào: {chart_path}")

if __name__ == '__main__':
    analyze_and_plot()
