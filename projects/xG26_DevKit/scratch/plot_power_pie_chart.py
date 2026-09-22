#!/usr/bin/env python3
"""
plot_power_pie_chart.py
Vẽ biểu đồ tròn phân bổ năng lượng tiêu thụ (Hardware Power Budget Breakdown)
theo đúng định dạng và thông số của dự án SomniGuard.
"""

import matplotlib.pyplot as plt
import os
import shutil
import sys

sys.stdout.reconfigure(encoding='utf-8')

def draw_pie_chart():
    # 1. Thông số dòng tiêu thụ các thành phần phần cứng (mA @ 3.3V)
    i_mcu = 0.91    # EFR32xG26 MCU (EM0 Active 78MHz + MVP AI + EM2 Deep Sleep)
    i_ppg = 1.60    # MAX30102 PPG (50Hz Red/IR pulsed LEDs)
    i_imu = 0.45    # MPU6050 IMU (50Hz low-power mode)
    i_ble = 0.28    # BLE 5.4 Radio (1Hz Telemetry TX)
    i_haptic = 1.60 # Haptic Motor (Duty-cycled burst ~5.3% khi ngưng thở)

    i_total = i_mcu + i_ppg + i_imu + i_ble + i_haptic # 4.84 mA

    # 2. Nhãn hiển thị và màu sắc
    labels = [
        'EFR32xG26 MCU\n(EM0/EM2/MVP)',
        'MAX30102 PPG\n(50Hz Red/IR)',
        'MPU6050 IMU\n(50Hz)',
        'BLE 5.4 Radio\n(Telemetry)',
        'Haptic Motor\n(Intervention)'
    ]
    currents = [i_mcu, i_ppg, i_imu, i_ble, i_haptic]
    colors = ['#8b5cf6', '#ef4444', '#3b82f6', '#10b981', '#f59e0b']
    explode = (0.05, 0.05, 0, 0, 0.05)

    # 3. Tạo figure chất lượng cao (300 DPI)
    fig, ax = plt.subplots(figsize=(7, 6), dpi=300)

    wedges, texts, autotexts = ax.pie(
        currents,
        labels=labels,
        autopct='%1.1f%%',
        startangle=140,
        colors=colors,
        explode=explode,
        textprops=dict(color="black", fontsize=11),
        wedgeprops=dict(edgecolor='white', linewidth=1.5)
    )

    # Chỉnh font chữ phần trăm bên trong miếng bánh
    for autotext in autotexts:
        autotext.set_fontsize(11)
        autotext.set_fontweight('normal')

    # Chỉnh tiêu đề
    ax.set_title(
        f'Hardware Power Budget Breakdown (Avg: {i_total:.2f} mA @ 3.3V)',
        fontsize=13,
        fontweight='bold',
        pad=15
    )

    plt.tight_layout()

    # 4. Lưu ra các đường dẫn cần thiết
    out_dir = 'specDocument/image'
    os.makedirs(out_dir, exist_ok=True)
    out_file = os.path.join(out_dir, 'hardware_power_budget_pie.png')
    plt.savefig(out_file, dpi=300, bbox_inches='tight')
    print(f"[OK] Đã lưu biểu đồ tại: {out_file}")

    # Copy vào artifacts để preview trong Antigravity nếu có
    artifact_dir = r"C:\Users\ASUS\.gemini\antigravity\brain\e618c347-cc53-448f-abcb-72c4db3abed9"
    if os.path.exists(artifact_dir):
        artifact_file = os.path.join(artifact_dir, 'hardware_power_budget_pie.png')
        shutil.copyfile(out_file, artifact_file)
        print(f"[OK] Đã copy biểu đồ vào artifacts: {artifact_file}")

if __name__ == '__main__':
    draw_pie_chart()
