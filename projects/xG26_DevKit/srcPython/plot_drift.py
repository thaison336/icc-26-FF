import serial
import time
import re
import argparse
import matplotlib.pyplot as plt

def monitor_and_plot(port, baudrate):
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"Connected to {port} at {baudrate} baud.")
    except Exception as e:
        print(f"Error opening serial port {port}: {e}")
        return

    pattern = re.compile(r"STATUS \| .*?Drift \(MAX:\s*(\d+),\s*IMU:\s*(\d+)\)")
    resync_pattern = re.compile(r"\[RESYNC\] (.*)")
    
    print("Monitoring started. Mạch đang xuất log liên tục mỗi 20ms.")
    print("Nhấn Ctrl+C bất cứ lúc nào để dừng lại và xem biểu đồ.")
    
    times = []
    drifts = []
    
    start_time = time.time()
    last_print = 0
    
    # Mở file log để ghi sự kiện xả mẫu (chế độ append)
    log_file = open("resync_events.log", "a", encoding="utf-8")
    log_file.write(f"\n--- Bắt đầu phiên lúc {time.strftime('%Y-%m-%d %H:%M:%S')} ---\n")
    
    try:
        while True:
            line = ser.readline()
            if line:
                try:
                    line_str = line.decode('utf-8').strip()
                except UnicodeDecodeError:
                    continue
                
                # Bắt log RESYNC và ghi ra file
                resync_match = resync_pattern.search(line_str)
                if resync_match:
                    timestamp = time.strftime('%H:%M:%S')
                    msg = f"[{timestamp}] [RESYNC] {resync_match.group(1)}"
                    print(f"\n{msg}") # In xuống dòng mới để không bị đè bởi lệnh in Drift
                    log_file.write(msg + "\n")
                    log_file.flush() # Ép ghi ngay xuống đĩa cứng
                
                match = pattern.search(line_str)
                if match:
                    max_rem = int(match.group(1))
                    imu_rem = int(match.group(2))
                    drift = max_rem - imu_rem
                    
                    elapsed = time.time() - start_time
                    times.append(elapsed)
                    drifts.append(drift)
                    
                    # Tránh print liên tục quá mức (giới hạn 10 lần/s để dễ nhìn)
                    if elapsed - last_print > 0.1:
                        print(f"\rElapsed: {elapsed:6.2f}s | Current Drift: {drift:3d} (MAX: {max_rem:3d}, IMU: {imu_rem:3d})", end="", flush=True)
                        last_print = elapsed
                    
    except KeyboardInterrupt:
        print("\nMonitoring stopped by user. Generating plot...")
    
    finally:
        log_file.write(f"--- Kết thúc phiên lúc {time.strftime('%Y-%m-%d %H:%M:%S')} ---\n")
        log_file.close()
        ser.close()
        
        if times:
            plt.figure(figsize=(10, 6))
            plt.plot(times, drifts, label='Drift (MAX30102 - IMU)', color='b', linewidth=2)
            plt.xlabel('Thời gian (giây)', fontsize=12)
            plt.ylabel('Độ lệch (số lượng mẫu)', fontsize=12)
            plt.title('Biểu đồ thể hiện độ trôi (Clock Drift) giữa 2 cảm biến', fontsize=14)
            plt.grid(True, linestyle='--', alpha=0.7)
            plt.axhline(0, color='red', linewidth=1, linestyle='--')
            plt.legend()
            
            # Save plot
            plt.savefig("drift_plot.png", dpi=300)
            print("Đã lưu biểu đồ vào file: drift_plot.png")
            plt.show()
        else:
            print("Chưa thu thập đủ dữ liệu để vẽ biểu đồ.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Monitor and Plot Drift")
    parser.add_argument("-p", "--port", required=True, help="Serial port (e.g., COM3, /dev/ttyUSB0)")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="Baudrate (default: 115200)")
    args = parser.parse_args()
    
    monitor_and_plot(args.port, args.baudrate)
