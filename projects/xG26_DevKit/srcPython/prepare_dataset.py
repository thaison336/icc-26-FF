#!/usr/bin/env python3
"""
prepare_dataset.py

Script bóc tách dữ liệu chuỗi thời gian 50Hz từ dataset.csv thành các cửa sổ trượt 30s
(stride 0.5s, tần số cập nhật 2Hz - 60 bước x 4 đặc trưng) cho dự án SomniGuard (IoT Challenge 2026).

Input Tensor X Shape: (N, 60, 4) gồm:
1. SpO2_est: Chỉ số bão hòa Oxy ước tính (%)
2. BPM_est: Nhịp tim ước tính (BPM)
3. PPG_AC_Norm_IR: Đặc trưng co mạch (AC_rms / DC_mean) kênh IR
4. motion_level: Độ lệch chuẩn gia tốc cựa tay (g)

Output Label Y Shape: (N,) gán theo Tail-Anchored Hybrid Strategy:
- R_total = Tỷ lệ mẫu nhãn 1.0 trong 1500 mẫu (30s)
- R_tail10 = Tỷ lệ mẫu nhãn 1.0 trong 500 mẫu cuối (10s)
- Y = 1 (Apnea Onset): R_tail10 >= 0.50 HOẶC R_total >= 0.40
- Y = 0 (Normal): R_total < 0.10 VÀ R_tail10 < 0.20
- Discard: Loại bỏ các cửa sổ ranh giới nhiễu chuyển tiếp

Xuất file nén: somniguard_train_dataset.npz chứa X (float32) và Y (int64)
"""

import sys
import os
import csv
import math
from typing import List, Dict, Tuple
import numpy as np

# Import SomniGuard DSP & Motion từ process_ppg_imu.py nếu có
try:
    from process_ppg_imu import SomniGuardDSP, SomniGuardMotion, parse_float, is_session_boundary, DSP_WINDOW_SIZE
except ImportError:
    DSP_WINDOW_SIZE = 128


def calculate_ppg_ac_norm_ir(dsp: SomniGuardDSP) -> float:
    """Tính đặc trưng co mạch PPG_AC_Norm_IR = AC_rms / DC_mean của kênh IR"""
    sum_sq_ir = sum(dsp.history_sq_ir)
    sum_dc_ir = sum(dsp.history_dc_ir)

    rms_ir = math.sqrt(sum_sq_ir / float(DSP_WINDOW_SIZE))
    mean_dc_ir = sum_dc_ir / float(DSP_WINDOW_SIZE)

    if mean_dc_ir > 0.0:
        return rms_ir / mean_dc_ir
    return 0.0


def parse_raw_sessions(csv_filepath: str) -> List[List[dict]]:
    """
    Đọc dataset.csv và chia thành các danh sách mẫu liên tục cho từng Session.
    """
    sessions: List[List[dict]] = []
    current_session: List[dict] = []
    current_user = None

    print(f"Đang đọc và phân đoạn session từ {csv_filepath}...")
    with open(csv_filepath, mode='r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for idx, row in enumerate(reader):
            full_str = " ".join([str(v) for v in row.values() if v is not None])
            if "---" in full_str or "NEW SESSION" in full_str or "CUT" in full_str:
                if current_session:
                    sessions.append(current_session)
                    current_session = []
                continue

            user_info = row.get('UserInfo', row.get('user', '')).strip() if row.get('UserInfo') or row.get('user') else None
            sample_id = parse_float(row, ['Sample ID', 'sample_id', 'id'], default=-1.0)

            if (user_info and current_user and user_info != current_user) or (sample_id == 0.0 and len(current_session) > 0):
                if current_session:
                    sessions.append(current_session)
                    current_session = []

            current_user = user_info

            raw_red_val = parse_float(row, ['RED', 'ppg_red', 'red', 'Red'], default=-1.0)
            raw_ir_val = parse_float(row, ['IR', 'ppg_ir', 'ir', 'Ir'], default=-1.0)
            if raw_red_val < 0 or raw_ir_val < 0:
                continue

            current_session.append(row)

    if current_session:
        sessions.append(current_session)

    print(f"Đã phân tách được {len(sessions)} phiên làm việc (sessions) liên tục từ dataset.")
    return sessions


def build_npz_dataset(csv_filepath: str, output_npz_path: str = "somniguard_train_dataset.npz", ppg_sample_rate: int = 50):
    """
    Xử lý các session, tạo cửa sổ trượt 30s (60 bước x 4 đặc trưng), áp dụng Tail-Anchored Hybrid Strategy
    và xuất file npz chuẩn (N, 60, 4).
    """
    sessions = parse_raw_sessions(csv_filepath)

    X_list: List[np.ndarray] = []
    Y_list: List[int] = []

    total_candidate_windows = 0
    kept_label_0 = 0
    kept_label_1 = 0
    discarded_windows = 0

    stride_raw_count = ppg_sample_rate // 2     # 25 mẫu thô cho 0.5s
    window_30s_raw_count = ppg_sample_rate * 30    # 1500 mẫu thô cho 30s
    window_steps = 60    # 60 bước 0.5s trong 30s

    for sess_idx, session_rows in enumerate(sessions):
        dsp = SomniGuardDSP()
        motion = SomniGuardMotion(sample_rate_hz=ppg_sample_rate)

        session_feature_steps: List[List[float]] = []
        session_raw_labels: List[int] = []

        current_stride_raw_count = 0
        last_spo2 = 98.0
        last_bpm = 75.0
        last_motion = 0.0

        # 1. Trích xuất chuỗi 4 đặc trưng (2Hz) và nhãn thô (50Hz) cho cả Session
        for idx, row in enumerate(session_rows):
            raw_red = int(parse_float(row, ['RED', 'ppg_red', 'red', 'Red'], default=100000))
            raw_ir = int(parse_float(row, ['IR', 'ppg_ir', 'ir', 'Ir'], default=100000))

            time_val = parse_float(row, ['Time', 'timestamp_ms', 'time', 'timestamp_s'], default=idx * (1000.0 / ppg_sample_rate))
            ts_ms = int(time_val * 1000) if time_val < 10000 else int(time_val)

            ax = parse_float(row, ['acc_x', 'ax', 'ACC_X', 'AccX'], default=0.0)
            ay = parse_float(row, ['acc_y', 'ay', 'ACC_Y', 'AccY'], default=0.0)
            az = parse_float(row, ['acc_z', 'az', 'ACC_Z', 'AccZ'], default=1.0)

            if abs(ax) > 10.0 or abs(ay) > 10.0 or abs(az) > 10.0:
                ax /= 1000.0
                ay /= 1000.0
                az /= 1000.0

            raw_label = parse_float(row, ['Label', 'label', 'LABEL'], default=0.0)
            session_raw_labels.append(1 if raw_label >= 1.0 else 0)

            has_new_stride, spo2, bpm, ac_ir, dc_ir = dsp.process_sample(raw_red, raw_ir, ts_ms)
            if has_new_stride:
                last_spo2 = spo2
                last_bpm = bpm

            motion_val = motion.process_sample(ax, ay, az)
            last_motion = motion_val

            current_stride_raw_count += 1

            if current_stride_raw_count >= stride_raw_count:
                ppg_ac_norm_ir = calculate_ppg_ac_norm_ir(dsp)
                
                # 4 Đặc trưng cốt lõi: [SpO2_est, BPM_est, PPG_AC_Norm_IR, motion_level]
                feature_vec = [last_spo2, float(last_bpm), ppg_ac_norm_ir, last_motion]
                session_feature_steps.append(feature_vec)
                current_stride_raw_count = 0

        # 2. Tạo cửa sổ trượt 30s (60 bước = 1500 mẫu thô) trên Session
        num_steps = len(session_feature_steps)
        if num_steps < window_steps:
            continue

        for i in range(num_steps - window_steps + 1):
            total_candidate_windows += 1

            # Ma trận 60 bước x 4 đặc trưng
            feat_matrix = np.array(session_feature_steps[i : i + window_steps], dtype=np.float32)

            # Lấy 1500 nhãn thô tương ứng với cửa sổ 30s
            raw_start_idx = i * stride_raw_count
            raw_end_idx = raw_start_idx + window_30s_raw_count
            window_raw_labels = session_raw_labels[raw_start_idx : raw_end_idx]

            if len(window_raw_labels) < window_30s_raw_count:
                continue

            # Tính R_total (1500 mẫu = 30s) và R_tail10 (500 mẫu cuối = 10s)
            r_total = sum(window_raw_labels) / float(window_30s_raw_count)
            r_tail10 = sum(window_raw_labels[-500:]) / 500.0

            # Áp dụng chiến lược gán nhãn Tail-Anchored Hybrid Strategy
            assigned_label = -1

            if r_tail10 >= 0.50 or r_total >= 0.40:
                assigned_label = 1
                kept_label_1 += 1
            elif r_total < 0.10 and r_tail10 < 0.20:
                assigned_label = 0
                kept_label_0 += 1
            else:
                # Discard cửa sổ ranh giới nhiễu chuyển tiếp
                discarded_windows += 1
                continue

            X_list.append(feat_matrix)
            Y_list.append(assigned_label)

    if not X_list:
        print("Lỗi: Không tạo được cửa sổ hợp lệ nào từ dataset!")
        return

    # 3. Chuyển đổi thành NumPy array chuẩn
    X = np.array(X_list, dtype=np.float32)  # Shape (N, 60, 4)
    Y = np.array(Y_list, dtype=np.int64)    # Shape (N,)

    # 4. Xuất file .npz nén
    np.savez_compressed(output_npz_path, X=X, Y=Y)
    print(f"\n=======================================================")
    print(f"  SOMNIGUARD AI DATASET (60x4) GENERATED SUCCESSFULLY")
    print(f"=======================================================")
    print(f"File đầu ra:        {output_npz_path}")
    print(f"Dung lượng file:     {os.path.getsize(output_npz_path) / (1024 * 1024):.2f} MB")
    print(f"Hình dạng Tensor X:  {X.shape} (dtype={X.dtype})")
    print(f"Hình dạng Nhãn Y:   {Y.shape} (dtype={Y.dtype})")
    print(f"-------------------------------------------------------")
    print(f"THỐNG KÊ CHI TIẾT CÁC CỬA SỔ (CLASS BALANCE):")
    print(f"Tổng cửa sổ trượt 30s đánh giá: {total_candidate_windows:,}")
    print(f"Tổng cửa sổ được lưu giữ (N):  {len(Y_list):,} ({(len(Y_list)/total_candidate_windows)*100:.2f}%)")
    print(f" - Nhãn 0 (Normal):             {kept_label_0:,} ({(kept_label_0/len(Y_list))*100:.2f}% của tập train)")
    print(f" - Nhãn 1 (Apnea/Hypopnea):     {kept_label_1:,} ({(kept_label_1/len(Y_list))*100:.2f}% của tập train)")
    print(f"Số cửa sổ ranh giới bị loại:    {discarded_windows:,} ({(discarded_windows/total_candidate_windows)*100:.2f}% tổng số)")
    print(f"=======================================================\n")


if __name__ == "__main__":
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')

    input_file = "dataset.csv"
    output_npz = "somniguard_train_dataset.npz"

    if len(sys.argv) > 1:
        input_file = sys.argv[1]
    if len(sys.argv) > 2:
        output_npz = sys.argv[2]

    build_npz_dataset(input_file, output_npz, ppg_sample_rate=50)
