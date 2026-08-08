import serial
import time
import re
import argparse
from datetime import datetime

def monitor_board(port, baudrate, expected_freq, timeout, log_file):
    try:
        ser = serial.Serial(port, baudrate, timeout=timeout)
        print(f"Connected to {port} at {baudrate} baud.")
    except Exception as e:
        print(f"Error opening serial port {port}: {e}")
        return

    pattern = re.compile(r"STATUS \| Freq:\s*(\d+)\s*Hz")
    drift_pattern = re.compile(r"Drift \(MAX:\s*(\d+),\s*IMU:\s*(\d+)\)")

    print(f"Monitoring started. Expected frequency: {expected_freq} Hz")
    print(f"Logging anomalies to {log_file}")
    print("Press Ctrl+C to stop.")

    start_time = time.time()
    last_receive_time = time.time()
    
    anomaly_counter = 0
    timeout_counter = 0
    
    max_drift = float('-inf')
    min_drift = float('inf')

    try:
        with open(log_file, "a") as f:
            f.write(f"\n=========================================\n")
            f.write(f"--- Monitoring Started at {datetime.now()} ---\n")
            f.flush()
            
            while True:
                line = ser.readline()
                current_time = time.time()

                if line:
                    try:
                        line_str = line.decode('utf-8').strip()
                    except UnicodeDecodeError:
                        continue
                    
                    if not line_str:
                        continue
                    
                    last_receive_time = current_time

                    match = pattern.search(line_str)
                    if match:
                        freq = int(match.group(1))
                        
                        # Tính toán drift
                        current_drift = None
                        drift_match = drift_pattern.search(line_str)
                        if drift_match:
                            max_rem = int(drift_match.group(1))
                            imu_rem = int(drift_match.group(2))
                            current_drift = max_rem - imu_rem
                            
                            if current_drift > max_drift:
                                max_drift = current_drift
                            if current_drift < min_drift:
                                min_drift = current_drift
                        
                        if freq < expected_freq * 0.9 or freq == 0:
                            anomaly_counter += 1
                            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                            log_msg = f"[{timestamp}] WARNING: Low Freq ({freq} Hz) | Raw: {line_str}"
                            print("\n" + log_msg)
                            f.write(log_msg + "\n")
                            f.flush()
                        else:
                            # In ra màn hình để biết mạch đang sống
                            drift_str = f"{current_drift:d} (Min: {min_drift:d}, Max: {max_drift:d})" if current_drift is not None else "N/A"
                            print(f"\rBoard running OK... Freq: {freq:3d} Hz | Drift: {drift_str} | Anomalies: {anomaly_counter} | Timeouts: {timeout_counter}", end="", flush=True)
                
                # Kiểm tra timeout (board bị treo không gửi dữ liệu)
                if current_time - last_receive_time > 5.0:  
                    timeout_counter += 1
                    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                    err_msg = f"\n[{timestamp}] ERROR: Timeout! No data received for 5 seconds."
                    print(err_msg)
                    
                    with open(log_file, "a") as f_timeout:
                        f_timeout.write(err_msg + "\n")
                    
                    last_receive_time = current_time

    except KeyboardInterrupt:
        print("\nMonitoring stopped by user.")
        
    finally:
        ser.close()
        elapsed = time.time() - start_time
        
        summary = f"\n--- Monitoring Summary ---\n"
        summary += f"End Time: {datetime.now()}\n"
        summary += f"Total time elapsed: {elapsed/3600:.2f} hours.\n"
        summary += f"Total anomalies (Low Freq): {anomaly_counter}\n"
        summary += f"Total timeouts (Hangs): {timeout_counter}\n"
        
        if max_drift != float('-inf') and min_drift != float('inf'):
            summary += f"Maximum Drift (MAX - IMU): {max_drift}\n"
            summary += f"Minimum Drift (MAX - IMU): {min_drift}\n"
        else:
            summary += f"Drift data: Not collected\n"
            
        summary += f"=========================================\n"
        
        print(summary)
        with open(log_file, "a") as f:
            f.write(summary)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Monitor Serial Log for SomniGuard Board")
    parser.add_argument("-p", "--port", required=True, help="Serial port (e.g., COM3, /dev/ttyUSB0)")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="Baudrate (default: 115200)")
    parser.add_argument("-f", "--freq", type=int, default=100, help="Expected frequency in Hz (default: 100)")
    parser.add_argument("-o", "--output", default="board_monitor.log", help="Output log file (default: board_monitor.log)")
    
    args = parser.parse_args()
    
    monitor_board(args.port, args.baudrate, args.freq, timeout=1.0, log_file=args.output)
