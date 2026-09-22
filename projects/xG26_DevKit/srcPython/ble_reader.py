"""
SomniGuard BLE Reader for Laptop (Windows / Linux / macOS)
Đọc dữ liệu phát qua Bluetooth Low Energy từ Kit xG26 (SomniGuard)
SomniGuard BLE Reader & CSV Logger for Laptop (Windows / Linux / macOS)
Đọc và lưu dữ liệu phát qua Bluetooth Low Energy từ Kit xG26 (SomniGuard) vào file CSV

Cài đặt thư viện trước khi chạy:
    pip install bleak

Cách chạy:
    python ble_reader.py
    python srcPython/ble_reader.py
    (Hoặc tùy chỉnh tên file CSV: python srcPython/ble_reader.py --csv my_data.csv)
"""

import asyncio
import struct
import sys
import os
import csv
import argparse
from datetime import datetime
from bleak import BleakScanner, BleakClient

# Thông số cấu hình BLE của dự án SomniGuard (xG26 DevKit)
TARGET_DEVICE_NAME = "SomniGuard"
SERVICE_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb"
CHAR_UUID_DATA = "0000ffe1-0000-1000-8000-00805f9b34fb"

# Mapping FSM States
FSM_TOP_STATES = {
    0: "ACTIVE_MODE",
    1: "NORMAL_SLEEP",
    2: "DEEP_ANALYSIS"
}

POSTURE_MAP = {
    0: "Supine (Ngửa)",
    1: "Prone (Sấp)",
    2: "Left Side (Nghiêng Trái)",
    3: "Right Side (Nghiêng Phải)",
    4: "Upright (Ngồi/Đứng)"
}

# Biến toàn cục quản lý ghi file CSV
csv_writer = None
csv_file_handle = None

def init_csv(filename: str):
    """Khởi tạo file CSV và ghi tiêu đề cột (header)"""
    global csv_writer, csv_file_handle
    file_exists = os.path.exists(filename)
    csv_file_handle = open(filename, mode="a", newline="", encoding="utf-8")
    csv_writer = csv.writer(csv_file_handle)
    
    if not file_exists or os.path.getsize(filename) == 0:
        csv_writer.writerow([
            "timestamp",
            "seq_num",
            "spo2_percent",
            "heart_rate_bpm",
            "motion_mg",
            "finger_attached",
            "signal_valid",
            "top_fsm_state",
            "sub_fsm_state",
            "battery_percent",
            "posture",
            "extra_info"
        ])
        csv_file_handle.flush()
    print(f"📁 Đang ghi dữ liệu vào file: {os.path.abspath(filename)}")

def log_telemetry_to_csv(timestamp, seq_num, spo2, hr, motion_mg, finger_attached, signal_valid, top_fsm_str, sub_fsm, battery, posture_str):
    """Ghi 1 dòng dữ liệu Telemetry vào CSV"""
    global csv_writer, csv_file_handle
    if csv_writer:
        csv_writer.writerow([
            timestamp,
            seq_num,
            f"{spo2:.2f}",
            f"{hr:.1f}",
            motion_mg,
            1 if finger_attached else 0,
            1 if signal_valid else 0,
            top_fsm_str,
            sub_fsm,
            battery,
            posture_str,
            ""
        ])
        csv_file_handle.flush()

def log_event_to_csv(timestamp, event_desc):
    """Ghi dòng sự kiện cảnh báo vào CSV"""
    global csv_writer, csv_file_handle
    if csv_writer:
        csv_writer.writerow([
            timestamp,
            "", "", "", "", "", "", "", "", "", "",
            event_desc
        ])
        csv_file_handle.flush()

def parse_telemetry_packet(data: bytes):
    """
    Giải mã gói tin Telemetry 12 bytes:
    struct somniguard_ble_telemetry_pkt_t {
        uint16_t seq_num;        // little-endian H (2B)
        uint16_t spo2_x100;      // H (2B)
        uint16_t hr_x10;         // H (2B)
        uint16_t motion_mg;      // H (2B)
        uint8_t  posture_flags;  // B (1B): bit 0-2: Posture, bit 3: Finger, bit 4: Valid
        uint8_t  top_fsm_state;  // B (1B)
        uint8_t  sub_fsm_state;  // B (1B)
        uint8_t  battery_level;  // B (1B)
    }
    """
    seq_num, spo2_raw, hr_raw, motion_mg, posture_flags, top_fsm, sub_fsm, battery = struct.unpack("<HHHHBBBB", data)
    
    spo2 = spo2_raw / 100.0
    hr = hr_raw / 10.0
    posture_id = posture_flags & 0x07
    finger_attached = bool(posture_flags & (1 << 3))
    signal_valid = bool(posture_flags & (1 << 4))
    
    posture_str = POSTURE_MAP.get(posture_id, f"Khác ({posture_id})")
    top_state_str = FSM_TOP_STATES.get(top_fsm, f"State_{top_fsm}")
    
    timestamp = datetime.now().strftime("%H:%M:%S")
    print(f"[{timestamp}] [TELEM #{seq_num:<5}] SpO2: {spo2:5.2f}% | HR: {hr:5.1f} BPM | "
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    time_display = datetime.now().strftime("%H:%M:%S")
    
    # In ra Terminal
    print(f"[{time_display}] [TELEM #{seq_num:<5}] SpO2: {spo2:5.2f}% | HR: {hr:5.1f} BPM | "
          f"Motion: {motion_mg:4} mg | FSM: {top_state_str:<13} | Finger: {'YES' if finger_attached else 'NO ':<3} | "
          f"Valid: {int(signal_valid)} | Pin: {battery:3}% | Tư thế: {posture_str}")
    
    # Ghi vào file CSV
    log_telemetry_to_csv(timestamp, seq_num, spo2, hr, motion_mg, finger_attached, signal_valid, top_state_str, sub_fsm, battery, posture_str)

def parse_event_packet(data: bytes):
    """
    Giải mã gói tin Event 8 bytes:
    struct somniguard_ble_event_pkt_t {
        uint8_t  event_type;
        uint8_t  event_code;
        uint16_t seq_num;
        uint16_t param1;
        uint16_t param2;
    }
    """
    evt_type, evt_code, seq_num, p1, p2 = struct.unpack("<BBHHH", data)
    timestamp = datetime.now().strftime("%H:%M:%S")
    print(f"[{timestamp}] [>>> EVENT ALERT <<<] Type: 0x{evt_type:02X}, Code: 0x{evt_code:02X}, "
          f"Seq: {seq_num}, Param1: {p1}, Param2: {p2}")
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    time_display = datetime.now().strftime("%H:%M:%S")
    desc = f"EVENT_ALERT: Type=0x{evt_type:02X}, Code=0x{evt_code:02X}, Seq={seq_num}, P1={p1}, P2={p2}"
    
    print(f"[{time_display}] [>>> {desc} <<<]")
    log_event_to_csv(timestamp, desc)

def notification_callback(sender, data: bytearray):
    raw_bytes = bytes(data)
    length = len(raw_bytes)
    
    if length == 12:
        parse_telemetry_packet(raw_bytes)
    elif length == 8:
        parse_event_packet(raw_bytes)
    else:
        # Chuỗi ký tự ASCII / UTF-8 (Ví dụ: "SOS" hoặc thông báo text)
        # Chuỗi ký tự (như "SOS" hoặc thông báo text)
        try:
            text = raw_bytes.decode('utf-8', errors='ignore').strip()
            timestamp = datetime.now().strftime("%H:%M:%S")
            print(f"[{timestamp}] [BLE TEXT] {text}")
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            time_display = datetime.now().strftime("%H:%M:%S")
            print(f"[{time_display}] [BLE TEXT] {text}")
            log_event_to_csv(timestamp, f"TEXT_MSG: {text}")
        except Exception:
            print(f"[RAW HEX] {raw_bytes.hex()}")

async def main():
async def main(csv_filename: str):
    print("=" * 70)
    print("       SOMNIGUARD BLE DATA RECEIVER (xG26 DevKit)")
    print("       SOMNIGUARD BLE DATA RECEIVER & CSV RECORDER")
    print("=" * 70)
    print(f"Đang quét tìm thiết bị Bluetooth '{TARGET_DEVICE_NAME}'...")

    
    # Khởi tạo file CSV
    init_csv(csv_filename)
    
    print(f"\nĐang quét tìm thiết bị Bluetooth '{TARGET_DEVICE_NAME}'...")
    device = None
    devices = await BleakScanner.discover(timeout=6.0)
    for d in devices:
        if d.name and TARGET_DEVICE_NAME.lower() in d.name.lower():
            device = d
            break

    if not device:
        print(f"\n❌ Không tìm thấy thiết bị '{TARGET_DEVICE_NAME}'.")
        print("\nKiểm tra lại:")
        print(" 1. Đảm bảo kit xG26 đã được cấp nguồn và đang chạy code.")
        print(" 2. Đảm bảo Bluetooth trên Laptop đang bật.")
        print(" 3. Đảm bảo kit không bị app điện thoại nào khác đang kết nối chiếm dụng kênh.")
        return

    print(f"✅ Đã tìm thấy: {device.name} [{device.address}]")
    print("Đang kết nối tới kit...")

    async with BleakClient(device) as client:
        if not client.is_connected:
            print("❌ Kết nối không thành công.")
            return

        print(f"✅ Đã kết nối thành công tới {device.name}!")
        print(f"📡 Đang bật Subscribe Notification (UUID: {CHAR_UUID_DATA})...")
        
        await client.start_notify(CHAR_UUID_DATA, notification_callback)
        print("🚀 Đang nhận luồng dữ liệu thời gian thực... (Nhấn Ctrl + C để dừng)\n" + "-" * 70)
        print(f"🚀 Đang nhận và lưu data vào CSV liên tục... (Nhấn Ctrl + C để dừng)\n" + "-" * 70)

        try:
            while True:
                await asyncio.sleep(1)
        except asyncio.CancelledError:
            pass
        finally:
            print("\nĐang dừng notification...")
            try:
                await client.stop_notify(CHAR_UUID_DATA)
            except Exception:
                pass
            print("Đã ngắt kết nối.")
            print("Đã ngắt kết nối an toàn.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SomniGuard BLE Data Receiver & CSV Logger")
    default_csv = f"somniguard_ble_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    parser.add_argument("--csv", type=str, default=default_csv, help=f"Tên file CSV lưu dữ liệu (mặc định: {default_csv})")
    args = parser.parse_args()

    try:
        asyncio.run(main())
        asyncio.run(main(args.csv))
    except KeyboardInterrupt:
        print("\nĐã dừng nhận dữ liệu BLE.")

        print("\nĐã dừng chương trình và đóng file CSV.")
    finally:
        if csv_file_handle and not csv_file_handle.closed:
            csv_file_handle.close()
            print("Đã lưu và đóng file CSV thành công.")
