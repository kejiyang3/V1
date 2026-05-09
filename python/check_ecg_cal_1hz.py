"""
MAX30003 内部 1Hz 校准波验证脚本

用法: python python/check_ecg_cal_1hz.py <CSV文件路径>

依赖: pip install pandas numpy matplotlib
"""
import sys, os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

FS = 512.0

def analyze(csv_file):
    df = pd.read_csv(csv_file)

    seq = df["seq"].to_numpy()
    ecg = df["ecg"].to_numpy().astype(float)

    # 用 seq 生成时间，不使用 timestamp_ms
    t = seq / FS

    # seq 连续性检查
    seq_diff = np.diff(seq)
    missing = np.where(seq_diff != 1)[0]

    # 文件名检查
    base = os.path.basename(csv_file).lower()
    if "cal" in base:
        print("WARNING: 文件名包含 cal(校准)，这是内部校准模式文件，不能代表真实外部输入。")
    if not any(k in base for k in ["open", "short", "human"]):
        print("WARNING: 文件名不包含 open/short/human，无法确定测试模式。")

    print("=" * 50)
    print("MAX30003 内部 1Hz 校准测试验证")
    print("=" * 50)
    print(f"文件: {csv_file}")
    print(f"分析采样率: {FS} Hz")
    print(f"样本数: {len(df)}")
    print(f"seq 起始: {seq[0]}")
    print(f"seq 结束: {seq[-1]}")
    print(f"seq 缺失数: {len(missing)}")
    if len(missing) > 0:
        print(f"seq 缺失位置(前10): {missing[:10]}")
    print(f"按 seq 推算时长: {(seq[-1] - seq[0] + 1) / FS:.2f}s")

    # 基本统计
    print(f"\n--- 信号统计 ---")
    print(f"最小值: {np.min(ecg):.1f}")
    print(f"最大值: {np.max(ecg):.1f}")
    print(f"峰峰值: {np.max(ecg) - np.min(ecg):.1f}")
    print(f"均值: {np.mean(ecg):.1f}")
    print(f"标准差: {np.std(ecg):.1f}")

    # FFT 分析
    ecg_dc = ecg - np.mean(ecg)
    n = len(ecg_dc)
    freq = np.fft.rfftfreq(n, d=1.0 / FS)
    mag = np.abs(np.fft.rfft(ecg_dc)) / n

    # 忽略 DC，找主频
    idx = np.argmax(mag[1:]) + 1
    dom_freq = freq[idx]
    dom_mag = mag[idx]

    # 找 Top 5 频率峰
    top5 = np.argsort(mag[1:])[-5:][::-1] + 1
    print(f"\n--- 频谱 (Top 5) ---")
    for i in top5:
        print(f"  {freq[i]:.3f} Hz: magnitude={mag[i]:.4f}")

    # 判断结果
    print(f"\n--- 检查结果 ---")
    if len(missing) == 0:
        print(f"  SEQ_CHECK: OK  (连续无缺失)")
        seq_ok = True
    else:
        print(f"  SEQ_CHECK: FAIL (缺失 {len(missing)} 个样本)")
        seq_ok = False

    if 0.8 <= dom_freq <= 1.2:
        print(f"  FREQ_CHECK: OK  (主频={dom_freq:.3f} Hz)")
        freq_ok = True
    else:
        print(f"  FREQ_CHECK: FAIL (主频={dom_freq:.3f} Hz, 期望 ~1Hz)")
        freq_ok = False

    print(f"  SEQ: {'PASS' if seq_ok else 'FAIL'}")
    print(f"  FREQ: {'PASS' if freq_ok else 'FAIL'}")
    print(f"  OVERALL: {'PASS' if (seq_ok and freq_ok) else 'FAIL'}")

    # 绘图
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))

    # 时域 - 显示前 10 秒
    window_10s = min(int(FS * 10), len(t))
    ax1.plot(t[:window_10s], ecg[:window_10s], linewidth=0.8)
    ax1.set_xlabel("Time (s)")
    ax1.set_ylabel("ECG raw")
    ax1.set_title("MAX30003 Internal 1Hz Calibration - Time Domain (first 10s)")
    ax1.grid(True)

    # 频谱
    ax2.plot(freq, mag, linewidth=0.8)
    ax2.set_xlim(0, 10)
    ax2.axvline(x=1.0, color='r', linestyle='--', alpha=0.5, label='1Hz expected')
    ax2.axvline(x=dom_freq, color='g', linestyle='--', alpha=0.5, label=f'{dom_freq:.2f}Hz detected')
    ax2.set_xlabel("Frequency (Hz)")
    ax2.set_ylabel("Magnitude")
    ax2.set_title("MAX30003 Internal 1Hz Calibration - FFT")
    ax2.legend()
    ax2.grid(True)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"用法: python {sys.argv[0]} <CSV文件>")
        sys.exit(1)

    csv_path = sys.argv[1]
    if not os.path.isfile(csv_path):
        print(f"[错误] 文件不存在: {csv_path}")
        sys.exit(1)

    analyze(csv_path)
