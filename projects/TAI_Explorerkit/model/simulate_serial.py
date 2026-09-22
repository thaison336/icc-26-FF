import serial
import time
import numpy as np
import argparse
from sklearn.metrics import accuracy_score, precision_score, recall_score, f1_score, confusion_matrix
import os

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_data = os.path.join(script_dir, '..', 'AI-model', 'test_windows.npz')
    
    parser = argparse.ArgumentParser(description="Evaluate SomniGuard Model over Serial")
    parser.add_argument('--port', type=str, default='COM12', help='Serial port (e.g. COM3)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate')
    parser.add_argument('--data', type=str, default=default_data, help='Path to test_windows.npz')
    args = parser.parse_args()

    # Load data
    print(f"Loading data from {args.data}...")
    try:
        data = np.load(args.data)
        X = data['X']
        y_true = data['label']
    except Exception as e:
        print(f"Failed to load data: {e}")
        return

    print(f"Data loaded: X shape={X.shape}, y shape={y_true.shape}")
    num_windows = X.shape[0]

    # Open serial port
    print(f"Opening {args.port} at {args.baud} baud...")
    try:
        ser = serial.Serial(args.port, args.baud, timeout=2.0)
    except Exception as e:
        print(f"Failed to open serial port: {e}")
        return

    # Let the board reset if needed
    time.sleep(2)
    ser.reset_input_buffer()

    y_pred = []
    conf_scores = []
    latencies = []

    print("Starting evaluation...")
    
    for i in range(num_windows):
        window = X[i]
        
        # Reset board buffer
        ser.write(b"RESET\n")
        
        # Wait for RESET_OK or timeout
        start_wait = time.time()
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if "RESET_OK" in line:
                break
            if time.time() - start_wait > 2.0:
                print(f"Warning: Did not receive RESET_OK for window {i}")
                break

        start_time = time.time()
        
        # Send 60 frames
        for frame in window:
            # frame is 28 floats
            # Format: W,val1,val2,...,val28\n
            frame_str = "W," + ",".join([f"{v:.4f}" for v in frame]) + "\n"
            ser.write(frame_str.encode('utf-8'))
            
            # Small delay to prevent UART buffer overflow on the board
            time.sleep(0.002) 

        # Wait for PRED and CONF
        pred_val = -1
        conf_val = 0.0
        
        # We might receive multiple lines (INF_START, INF_PRE_INVOKE, etc)
        # Timeout after 5 seconds for a window
        wait_pred_start = time.time()
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line.startswith("PRED:"):
                    try:
                        pred_val = int(line.split(":")[1])
                    except:
                        pass
                elif line.startswith("CONF:"):
                    try:
                        conf_val = float(line.split(":")[1])
                    except:
                        pass
                    # Assuming CONF comes after PRED or together, we break here
                    break
            
            if time.time() - wait_pred_start > 5.0:
                print(f"Timeout waiting for prediction on window {i}")
                break
                
        end_time = time.time()
        latency = end_time - start_time
        
        if pred_val != -1:
            y_pred.append(pred_val)
            conf_scores.append(conf_val)
            latencies.append(latency)
            print(f"Window {i+1}/{num_windows} - True: {int(y_true[i])}, Pred: {pred_val}, Conf: {conf_val:.4f}, Latency: {latency:.3f}s")
        else:
            print(f"Window {i+1}/{num_windows} - Failed to get prediction")
            y_pred.append(0) # Default to 0 on failure
            conf_scores.append(0.0)

    ser.close()

    # Calculate metrics
    print("\n" + "="*40)
    print("EVALUATION RESULTS")
    print("="*40)
    
    # Ensure y_true matches y_pred length (in case of early termination, but here we process all)
    y_true_eval = y_true[:len(y_pred)]
    
    acc = accuracy_score(y_true_eval, y_pred)
    prec = precision_score(y_true_eval, y_pred, zero_division=0)
    rec = recall_score(y_true_eval, y_pred, zero_division=0)
    f1 = f1_score(y_true_eval, y_pred, zero_division=0)
    cm = confusion_matrix(y_true_eval, y_pred)
    
    avg_latency = np.mean(latencies) if latencies else 0.0

    print(f"Accuracy : {acc:.4f}")
    print(f"Precision: {prec:.4f}")
    print(f"Recall   : {rec:.4f}")
    print(f"F1-Score : {f1:.4f}")
    print(f"Avg Latency: {avg_latency:.3f} s / window")
    print("Confusion Matrix:")
    print(cm)

if __name__ == "__main__":
    main()
