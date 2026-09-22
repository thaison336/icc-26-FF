#!/usr/bin/env python3
"""
inspect_dataset.py

Script kiểm tra và xem nội dung file dữ liệu huấn luyện somniguard_train_dataset.npz (N, 60, 28)
"""

import sys
import numpy as np

def inspect_npz(filepath: str = "somniguard_train_dataset.npz"):
    print(f"=======================================================")
    print(f"  KIỂM TRA FILE DATASET: {filepath}")
    print(f"=======================================================\n")

    try:
        data = np.load(filepath)
    except Exception as e:
        print(f"Lỗi khi đọc file {filepath}: {e}")
        return

    print("Các mảng (keys) trong file .npz:", list(data.keys()))
    
    X = data['X']  # Shape: (N, 60, 28)
    Y = data['Y']  # Shape: (N,)

    print(f"\n--- 1. THÔNG SỐ TỔNG QUAN ---")
    print(f"Mảng Tensor X (Features): Shape = {X.shape}, kiểu dữ liệu = {X.dtype} (60 bước x 28 đặc trưng)")
    print(f"Mảng Nhãn Y (Labels):     Shape = {Y.shape}, kiểu dữ liệu = {Y.dtype}")
    print(f"Tổng số mẫu 30s (N):       {len(Y)}")

    print(f"\n--- 2. THỐNG KÊ PHÂN BỐ NHÃN Y (3 LỚP) ---")
    unique_y, counts_y = np.unique(Y, return_counts=True)
    label_map = {
        0: "Normal (0)",
        1: "Mild / Onset Apnea (1)",
        2: "Severe / Critical Apnea (2)"
    }
    for lbl, cnt in zip(unique_y, counts_y):
        lbl_name = label_map.get(int(lbl), f"Unknown ({lbl})")
        print(f" - Nhãn {lbl} ({lbl_name}): {cnt} mẫu ({cnt/len(Y)*100:.2f}%)")

    feature_names = ["SpO2_est", "BPM_est"] + [f"PPG_IR_AC_{i}" for i in range(25)] + ["motion_level"]
    print(f"\n--- 3. DANH SÁCH 28 ĐẶC TRƯNG TRONG MỖI BƯỚC ---")
    for i, name in enumerate(feature_names):
        print(f" Cột [{i:2d}]: {name:<16}", end="\n" if (i+1)%4==0 else " | ")
    print()

    print(f"\n--- 4. XEM THỬ MẪU ĐẦU TIÊN X[0] (Ma trận 60 bước x 28 đặc trưng) ---")
    lbl_0 = int(Y[0])
    print(f"Nhãn tương ứng Y[0] = {lbl_0} ({label_map.get(lbl_0, 'Unknown')})")
    print(f"Bước 0 (0.5s đầu tiên):")
    print(f"  SpO2_est:        {X[0, 0, 0]:.2f}%")
    print(f"  BPM_est:         {int(X[0, 0, 1])}")
    print(f"  PPG_IR_AC (25):  [{X[0, 0, 2]:.4f}, {X[0, 0, 3]:.4f}, ..., {X[0, 0, 26]:.4f}]")
    print(f"  motion_level:    {X[0, 0, 27]:.4f} g")

    print(f"\nBước 59 (0.5s cuối cùng của cửa sổ 30s):")
    print(f"  SpO2_est:        {X[0, 59, 0]:.2f}%")
    print(f"  BPM_est:         {int(X[0, 59, 1])}")
    print(f"  PPG_IR_AC (25):  [{X[0, 59, 2]:.4f}, {X[0, 59, 3]:.4f}, ..., {X[0, 59, 26]:.4f}]")
    print(f"  motion_level:    {X[0, 59, 27]:.4f} g")

    print(f"\n--- 5. GIÁ TRỊ TỐI ĐA / TỐI THIỂU CỦA CÁC ĐẶC TRƯNG CHÍNH ---")
    print(f"{'Đặc trưng':<18} | {'Min':<10} | {'Max':<10} | {'Mean':<10}")
    print("-" * 55)
    key_indices = [0, 1, 2, 14, 26, 27]
    for idx in key_indices:
        f_vals = X[:, :, idx]
        print(f"{feature_names[idx]:<18} | {np.min(f_vals):<10.4f} | {np.max(f_vals):<10.4f} | {np.mean(f_vals):<10.4f}")

if __name__ == "__main__":
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')

    filepath = sys.argv[1] if len(sys.argv) > 1 else "somniguard_train_dataset.npz"
    inspect_npz(filepath)
