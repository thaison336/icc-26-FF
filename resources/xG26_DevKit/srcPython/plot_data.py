import serial
import time
import argparse
import matplotlib.pyplot as plt
from collections import deque
import matplotlib.animation as animation

# Độ phân giải hiển thị (250 mẫu = 5 giây ở tần số 50Hz)
MAX_POINTS = 250 

times = deque(maxlen=MAX_POINTS)
acc_x = deque(maxlen=MAX_POINTS)
acc_y = deque(maxlen=MAX_POINTS)
acc_z = deque(maxlen=MAX_POINTS)
gyro_x = deque(maxlen=MAX_POINTS)
gyro_y = deque(maxlen=MAX_POINTS)
gyro_z = deque(maxlen=MAX_POINTS)
ppg_red = deque(maxlen=MAX_POINTS)
ppg_ir = deque(maxlen=MAX_POINTS)

def parse_line(line_str):
    """
    Phân tích dòng log có dạng:
    DATA|ax|ay|az|gx|gy|gz|red|ir
    """
    if line_str.startswith("DATA|"):
        parts = line_str.split('|')
        if len(parts) == 9:
            try:
                ax, ay, az = int(parts[1]), int(parts[2]), int(parts[3])
                gx, gy, gz = int(parts[4]), int(parts[5]), int(parts[6])
                red, ir = int(parts[7]), int(parts[8])
                return (ax, ay, az, gx, gy, gz, red, ir)
            except ValueError:
                return None
    return None

def monitor_and_plot(port, baudrate):
    try:
        ser = serial.Serial(port, baudrate, timeout=0.1)
        print(f"Đã kết nối tới cổng {port} với tốc độ {baudrate} baud.")
    except Exception as e:
        print(f"Lỗi mở cổng serial {port}: {e}")
        return

    print("Đang lắng nghe dữ liệu... Bấm Ctrl+C (hoặc đóng cửa sổ đồ thị) để thoát.")
    
    # Thiết lập giao diện đồ thị (2 biểu đồ xếp dọc tập trung vào PPG)
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))
    fig.tight_layout(pad=3.0)

    # Khởi tạo các đường vẽ
    line_red, = ax1.plot([], [], label='Red', color='red')
    ax1.legend(loc='upper right')
    ax1.set_title('Nhịp tim - PPG Red')
    ax1.set_ylabel('Value')

    line_ir, = ax2.plot([], [], label='IR', color='black')
    ax2.legend(loc='upper right')
    ax2.set_title('Nhịp tim - PPG IR')
    ax2.set_ylabel('Value')
    ax2.set_xlabel('Mẫu thời gian')

    t = 0

    def update(frame):
        nonlocal t
        # Rút toàn bộ dữ liệu hiện có trong buffer Serial của máy tính
        while ser.in_waiting:
            line = ser.readline()
            if line:
                try:
                    line_str = line.decode('utf-8', errors='ignore').strip()
                    data = parse_line(line_str)
                    if data:
                        times.append(t)
                        acc_x.append(data[0])
                        acc_y.append(data[1])
                        acc_z.append(data[2])
                        gyro_x.append(data[3])
                        gyro_y.append(data[4])
                        gyro_z.append(data[5])
                        ppg_red.append(data[6])
                        ppg_ir.append(data[7])
                        t += 1
                    elif line_str.startswith("[RESYNC]"):
                        # Vẫn in log cảnh báo Resync ra Terminal
                        print(line_str)
                except Exception:
                    pass
        
        # Cập nhật dữ liệu mới lên biểu đồ
        if len(times) > 0:
            line_red.set_data(times, ppg_red)
            ax1.relim()
            ax1.autoscale_view()

            line_ir.set_data(times, ppg_ir)
            ax2.relim()
            ax2.autoscale_view()

        return line_red, line_ir

    # Tạo Animation tự động cập nhật mỗi 50ms (khoảng 20 fps)
    ani = animation.FuncAnimation(fig, update, interval=50, cache_frame_data=False)
    plt.show()
    
    print("Đóng cổng Serial...")
    ser.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Live Plot Sensor Data")
    parser.add_argument("-p", "--port", required=True, help="Serial port (ví dụ: COM3)")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="Baudrate (mặc định: 115200)")
    args = parser.parse_args()
    
    monitor_and_plot(args.port, args.baudrate)
