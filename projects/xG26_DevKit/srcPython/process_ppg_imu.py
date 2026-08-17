#!/usr/bin/env python3
"""
process_ppg_imu.py

Python DSP & Motion Processing Engine matching somniguard_dsp.cpp and somniguard_motion.cpp
"""

import math
from typing import List, Tuple, Dict, Any

DSP_WINDOW_SIZE = 128
DSP_STRIDE = 25
BPM_FILTER_SIZE = 5
BPM_DEFAULT = 75

HPF_ALPHA = 0.97
LPF_BETA = 0.50

SPO2_A = 100.00
SPO2_B = -2.50
SPO2_C = 18.75
SPO2_SMOOTH = 0.40
SQI_MIN_PI = 0.001
SQI_MAX_PI = 0.200
MAX_SPO2_DROP = 2.50


def parse_float(row: dict, keys: List[str], default: float = 0.0) -> float:
    for k in keys:
        if k in row and row[k] is not None and str(row[k]).strip() != "":
            try:
                return float(row[k])
            except ValueError:
                pass
    return default


def is_session_boundary(row: dict) -> bool:
    full_str = " ".join([str(v) for v in row.values() if v is not None])
    return ("---" in full_str or "NEW SESSION" in full_str or "CUT" in full_str)


class SomniGuardDSP:
    def __init__(self):
        self.reset()

    def reset(self):
        self.dc_track_red = 0.0
        self.dc_track_ir = 0.0
        self.lpf_red_prev = 0.0
        self.lpf_ir_prev = 0.0
        self.is_finger_attached = False

        # BPM
        self.bpm_state = 0  # 0: WAIT_FOR_DIP, 1: HUNTING_VALLEY
        self.valley_min_val = 0.0
        self.valley_min_time = 0
        self.last_beat_time = 0
        self.samples_since_last_beat = 0
        self.local_ac_ir_min = 0.0
        self.beat_threshold = -150.0
        self.bpm_index = 0
        self.smoothed_bpm = BPM_DEFAULT
        self.bpm_history = [BPM_DEFAULT] * BPM_FILTER_SIZE

        # Circular buffer
        self.history_sq_red = [0.0] * DSP_WINDOW_SIZE
        self.history_sq_ir = [0.0] * DSP_WINDOW_SIZE
        self.history_dc_red = [0.0] * DSP_WINDOW_SIZE
        self.history_dc_ir = [0.0] * DSP_WINDOW_SIZE
        self.circular_index = 0
        self.sample_count = 0
        self.stride_counter = 0
        self.is_buffer_full = False

        self.final_r = 0.0
        self.final_spo2 = 98.0
        self.is_first_calc = True
        self.buf_spo2 = [98.0] * 5

    def _apply_hampel_filter(self, new_val: float) -> float:
        self.buf_spo2.pop(0)
        self.buf_spo2.append(new_val)
        sorted_x = sorted(self.buf_spo2)
        median_m = sorted_x[2]
        dev = sorted([abs(x - median_m) for x in self.buf_spo2])
        mad = dev[2]
        threshold = max(2.5, 3.0 * 1.4826 * mad)
        if abs(new_val - median_m) > threshold:
            return median_m
        return new_val

    def _update_bpm(self, ac_ir_filtered: float, timestamp_ms: int):
        self.samples_since_last_beat += 1
        if ac_ir_filtered < self.local_ac_ir_min:
            self.local_ac_ir_min = ac_ir_filtered

        if self.bpm_state == 0:  # WAIT_FOR_DIP
            if ac_ir_filtered < self.beat_threshold and self.samples_since_last_beat > 15:
                self.bpm_state = 1  # HUNTING_VALLEY
                self.valley_min_val = ac_ir_filtered
                self.valley_min_time = timestamp_ms
        elif self.bpm_state == 1:  # HUNTING_VALLEY
            if ac_ir_filtered < self.valley_min_val:
                self.valley_min_val = ac_ir_filtered
                self.valley_min_time = timestamp_ms
            if ac_ir_filtered > (self.valley_min_val + 25.0):
                delta_time = self.valley_min_time - self.last_beat_time
                if 375 < delta_time < 1500:
                    instant_bpm = 60000.0 / delta_time
                    self.bpm_history[self.bpm_index] = int(instant_bpm)
                    self.bpm_index = (self.bpm_index + 1) % BPM_FILTER_SIZE
                    self.smoothed_bpm = sum(self.bpm_history) // BPM_FILTER_SIZE
                self.last_beat_time = self.valley_min_time
                self.beat_threshold = self.valley_min_val * 0.60
                self.local_ac_ir_min = 0.0
                self.samples_since_last_beat = 0
                self.bpm_state = 0

        if self.samples_since_last_beat > 100:
            self.beat_threshold = max(-50.0, self.local_ac_ir_min * 0.5)
            self.local_ac_ir_min = 0.0
            self.samples_since_last_beat = 0
            self.last_beat_time = timestamp_ms
            self.bpm_state = 0

    def _calculate_spo2(self) -> Tuple[bool, float, float]:
        sum_sq_red = sum(self.history_sq_red)
        sum_sq_ir = sum(self.history_sq_ir)
        sum_dc_red = sum(self.history_dc_red)
        sum_dc_ir = sum(self.history_dc_ir)

        rms_red = math.sqrt(sum_sq_red / float(DSP_WINDOW_SIZE))
        rms_ir = math.sqrt(sum_sq_ir / float(DSP_WINDOW_SIZE))
        mean_dc_red = sum_dc_red / float(DSP_WINDOW_SIZE)
        mean_dc_ir = sum_dc_ir / float(DSP_WINDOW_SIZE)

        if rms_ir > 0.0 and mean_dc_red > 0.0 and mean_dc_ir > 0.0:
            pi_ir = rms_ir / mean_dc_ir
            pi_red = rms_red / mean_dc_red
            sqi_ok = (SQI_MIN_PI <= pi_ir <= SQI_MAX_PI) and (SQI_MIN_PI <= pi_red <= SQI_MAX_PI)

            instant_r = (rms_red / mean_dc_red) / (rms_ir / mean_dc_ir)
            instant_spo2 = SPO2_A - (SPO2_B * instant_r) - (SPO2_C * instant_r * instant_r)
            instant_spo2 = max(50.0, min(100.0, instant_spo2))

            if self.is_first_calc:
                self.final_spo2 = instant_spo2
                self.final_r = instant_r
                self.buf_spo2 = [instant_spo2] * 5
                self.is_first_calc = False
            elif sqi_ok:
                spo2_new = SPO2_SMOOTH * instant_spo2 + (1.0 - SPO2_SMOOTH) * self.final_spo2
                if spo2_new < self.final_spo2 - MAX_SPO2_DROP:
                    spo2_new = self.final_spo2 - MAX_SPO2_DROP
                self.final_spo2 = self._apply_hampel_filter(spo2_new)
                self.final_r = SPO2_SMOOTH * instant_r + (1.0 - SPO2_SMOOTH) * self.final_r

        return True, self.final_spo2, self.final_r

    def process_sample(self, raw_red: int, raw_ir: int, timestamp_ms: int) -> Tuple[bool, float, int, float, float]:
        """
        Process single raw sample at 50Hz.
        Returns: (has_new_stride, spo2, bpm, ac_ir, dc_ir)
        """
        red_f = float(raw_red)
        ir_f = float(raw_ir)

        if not self.is_finger_attached:
            self.dc_track_red = red_f
            self.dc_track_ir = ir_f
            self.lpf_red_prev = 0.0
            self.lpf_ir_prev = 0.0
            self.is_finger_attached = True
            self.is_first_calc = True
            self.last_beat_time = timestamp_ms
            self.samples_since_last_beat = 0
            return False, self.final_spo2, self.smoothed_bpm, 0.0, self.dc_track_ir

        ac_red_raw = red_f - self.dc_track_red
        self.dc_track_red = (1.0 - HPF_ALPHA) * red_f + HPF_ALPHA * self.dc_track_red
        ac_ir_raw = ir_f - self.dc_track_ir
        self.dc_track_ir = (1.0 - HPF_ALPHA) * ir_f + HPF_ALPHA * self.dc_track_ir

        ac_red_filtered = (1.0 - LPF_BETA) * ac_red_raw + LPF_BETA * self.lpf_red_prev
        self.lpf_red_prev = ac_red_filtered
        ac_ir_filtered = (1.0 - LPF_BETA) * ac_ir_raw + LPF_BETA * self.lpf_ir_prev
        self.lpf_ir_prev = ac_ir_filtered

        self._update_bpm(ac_ir_filtered, timestamp_ms)

        idx = self.circular_index
        self.history_sq_red[idx] = ac_red_filtered * ac_red_filtered
        self.history_sq_ir[idx] = ac_ir_filtered * ac_ir_filtered
        self.history_dc_red[idx] = self.dc_track_red
        self.history_dc_ir[idx] = self.dc_track_ir

        self.circular_index = (self.circular_index + 1) % DSP_WINDOW_SIZE
        self.sample_count += 1
        self.stride_counter += 1

        if not self.is_buffer_full and self.sample_count >= DSP_WINDOW_SIZE:
            self.is_buffer_full = True

        if self.is_buffer_full and self.stride_counter >= DSP_STRIDE:
            self.stride_counter = 0
            _, cur_spo2, _ = self._calculate_spo2()
            return True, cur_spo2, self.smoothed_bpm, ac_ir_filtered, self.dc_track_ir

        return False, self.final_spo2, self.smoothed_bpm, ac_ir_filtered, self.dc_track_ir


class SomniGuardMotion:
    def __init__(self, sample_rate_hz: int = 50, window_size: int = 50, threshold: float = 0.15):
        self.sample_rate_hz = sample_rate_hz
        self.window_size = window_size
        self.threshold = threshold
        self.ring_buffer = [0.0] * window_size
        self.head = 0
        self.count = 0
        self.sum_a = 0.0
        self.sum_sq_a = 0.0

    def process_sample(self, ax: float, ay: float, az: float) -> float:
        mag = math.sqrt(ax * ax + ay * ay + az * az)

        if self.count < self.window_size:
            self.ring_buffer[self.head] = mag
            self.sum_a += mag
            self.sum_sq_a += mag * mag
            self.head = (self.head + 1) % self.window_size
            self.count += 1
        else:
            old_val = self.ring_buffer[self.head]
            self.sum_a += mag - old_val
            self.sum_sq_a += (mag * mag) - (old_val * old_val)
            self.ring_buffer[self.head] = mag
            self.head = (self.head + 1) % self.window_size

        if self.count < 2:
            return 0.0

        n = float(self.count)
        mean_a = self.sum_a / n
        var_a = (self.sum_sq_a / n) - (mean_a * mean_a)
        std_a = math.sqrt(max(0.0, var_a))
        return std_a
