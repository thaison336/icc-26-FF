import matplotlib
matplotlib.use("Agg")
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

NPZ_PATH = r"D:\STUDY\IOT_Challenge\icc-26-FF\projects\xG26_DevKit\model\test_windows.npz"
N_WINDOWS = 5  # so cua so can xem

data = np.load(NPZ_PATH)
X = data["X"]        # (825, 60, 28)
label = data["label"]  # (825,)

# Lay chi so cac cua so co label = 1
apnea_idx = np.where(label == 1)[0]
print(f"Total apnea windows (label=1): {len(apnea_idx)}")

selected = apnea_idx[:N_WINDOWS]

fig = plt.figure(figsize=(18, N_WINDOWS * 4.5))
fig.suptitle("Apnea Windows (label=1) — SpO2 / BPM / PPG Waveform", fontsize=15, fontweight="bold", y=1.01)

for plot_i, win_idx in enumerate(selected):
    window = X[win_idx]          # (60, 28)
    spo2   = window[:, 0]        # col 0: SpO2
    bpm    = window[:, 1]        # col 1: BPM
    ppg    = window[:, 2:28]     # col 2-27: 26 ppg features
    t      = np.arange(60) * 0.1  # 10Hz → 0..5.9 s theo label giây

    gs = gridspec.GridSpec(N_WINDOWS, 3, figure=fig,
                           left=0.06, right=0.97,
                           top=0.97, bottom=0.04,
                           hspace=0.55, wspace=0.35)

    # --- SpO2 ---
    ax1 = fig.add_subplot(gs[plot_i, 0])
    ax1.plot(t, spo2, color="#EF476F", linewidth=1.8)
    ax1.axhline(95, color="gray", linestyle="--", linewidth=0.8, label="95% threshold")
    ax1.axhline(90, color="orange", linestyle="--", linewidth=0.8, label="90% threshold")
    ax1.set_ylim(70, 102)
    ax1.set_title(f"Window #{win_idx}  SpO2", fontsize=10)
    ax1.set_ylabel("SpO2 (%)")
    ax1.set_xlabel("Time (s)")
    ax1.legend(fontsize=7, loc="lower right")
    ax1.fill_between(t, spo2, 95, where=(spo2 < 95), alpha=0.2, color="#EF476F")

    # --- BPM ---
    ax2 = fig.add_subplot(gs[plot_i, 1])
    ax2.plot(t, bpm, color="#118AB2", linewidth=1.8)
    ax2.set_title(f"Window #{win_idx}  BPM", fontsize=10)
    ax2.set_ylabel("BPM")
    ax2.set_xlabel("Time (s)")

    # stats text
    spo2_min = spo2.min()
    spo2_drop = spo2.max() - spo2_min
    ax2.text(0.02, 0.95,
             f"SpO2 min={spo2_min:.1f}%\ndrop={spo2_drop:.1f}%\nBPM range {bpm.min():.0f}-{bpm.max():.0f}",
             transform=ax2.transAxes, fontsize=8, va="top",
             bbox=dict(boxstyle="round", facecolor="#E8F4FD", alpha=0.8))

    # --- PPG waveform (trung binh cac feature) ---
    ax3 = fig.add_subplot(gs[plot_i, 2])
    ppg_mean = ppg.mean(axis=1)   # trung binh 26 kenh -> 1 duong
    ax3.plot(t, ppg_mean, color="#06D6A0", linewidth=1.8, label="mean(PPG features)")
    ax3.set_title(f"Window #{win_idx}  PPG (mean of 26 features)", fontsize=10)
    ax3.set_ylabel("Amplitude")
    ax3.set_xlabel("Time (s)")

    # to mau nen theo label
    ax3.text(0.02, 0.95, "APNEA (label=1)", transform=ax3.transAxes,
             fontsize=9, va="top", color="white",
             bbox=dict(boxstyle="round", facecolor="#EF476F", alpha=0.9))

OUT = r"C:\Users\ASUS\.gemini\antigravity-ide\brain\a7631491-a04f-4e3d-b1ef-1b357fed6905\scratch\apnea_windows.png"
plt.savefig(OUT, dpi=130, bbox_inches="tight")
print(f"Saved: {OUT}")
