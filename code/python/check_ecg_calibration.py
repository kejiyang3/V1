"""
MAX30003 内部校准波 (VCAL) 验证工具
用法: python python/check_ecg_calibration.py <ecg_cal_xxx.csv>

判断标准:
- 时域应显示稳定周期方波
- 主频应接近 1Hz (或设置的 FCAL 频率)
- 如果不是 1Hz 而是 9.7Hz -> MAX30003 配置或 SPI/FIFO 解析可能仍有问题
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import sys
import os

FS = 512.0  # MAX30003 采样率

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    fpath = sys.argv[1]
    if not os.path.isfile(fpath):
        print(f"[错误] 文件不存在: {fpath}")
        sys.exit(1)

    df = pd.read_csv(fpath)
    print(f"\n文件: {os.path.basename(fpath)}")
    print(f"总行数: {len(df)}")

    if "seq" not in df.columns or "ecg" not in df.columns:
        print("[错误] 缺少 seq 或 ecg 列")
        sys.exit(1)

    seq = df["seq"].to_numpy()
    ecg = df["ecg"].to_numpy().astype(float)
    ts = seq / FS  # 每秒 FS 个样本

    n = len(ecg)
    duration = ts[-1] - ts[0]

    # ====== 基本统计 ======
    v_min = np.min(ecg)
    v_max = np.max(ecg)
    v_pp = v_max - v_min
    v_mean = np.mean(ecg)
    v_std = np.std(ecg)

    print(f"\n--- 基本统计 ---")
    print(f"  样本数:     {n}")
    print(f"  时长:       {duration:.2f} 秒")
    print(f"  最小值:     {v_min:.1f}")
    print(f"  最大值:     {v_max:.1f}")
    print(f"  峰峰值:     {v_pp:.1f}")
    print(f"  均值:       {v_mean:.2f}")
    print(f"  标准差:     {v_std:.2f}")

    # ====== 去直流 ======
    ecg_ac = ecg - v_mean

    # ====== FFT 频谱分析 ======
    window = np.hanning(n)
    fft_vals = np.fft.rfft(ecg_ac * window)
    fft_mag = np.abs(fft_vals) / n
    freqs = np.fft.rfftfreq(n, d=1.0 / FS)

    # 找主频 (忽略 DC)
    idx_main = np.argmax(fft_mag[1:]) + 1
    dom_freq = freqs[idx_main]
    dom_mag = fft_mag[idx_main]

    print(f"\n--- 频谱分析 ---")
    print(f"  主频:       {dom_freq:.4f} Hz")
    print(f"  主频幅值:   {dom_mag:.4f}")

    # 前 5 个主频
    top5 = np.argsort(fft_mag[1:])[-5:][::-1] + 1
    print(f"  前 5 主频:")
    for i, idx in enumerate(top5):
        print(f"    {i+1}. {freqs[idx]:.4f} Hz  (幅值 {fft_mag[idx]:.4f})")

    # ====== 周期检测 ======
    # 通过过零检测估算周期
    signs = np.sign(ecg_ac)
    zero_crossings = np.where(np.diff(signs) != 0)[0]
    if len(zero_crossings) > 4:
        # 计算平均半周期 (从正到负或负到正)
        intervals = np.diff(zero_crossings)
        mean_half_period = np.mean(intervals) / FS
        est_freq = 1.0 / (2 * mean_half_period)
        print(f"\n--- 过零周期检测 ---")
        print(f"  过零次数:   {len(zero_crossings)}")
        print(f"  估算频率:   {est_freq:.4f} Hz")
    else:
        est_freq = 0
        print(f"\n--- 过零周期检测 ---")
        print(f"  过零次数极少，无法估算周期")

    # ====== 判断 ======
    print(f"\n{'='*50}")
    print(f"  判断")
    print(f"{'='*50}")

    # 预期的校准频率
    expected_freq = 1.0  # FCAL=100 时约 1Hz

    # 检查峰峰值 (内部 0.5mV 经 ADC 量化)
    if v_pp < 1:
        print(f"  [FAIL] 峰峰值 {v_pp:.1f}，信号几乎为零")
        print(f"         校准信号未进入 ADC 或 ADC 未工作")
    elif v_pp < 100:
        print(f"  [OK]   峰峰值 {v_pp:.1f}，在合理范围")
    else:
        print(f"  [WARN] 峰峰值 {v_pp:.1f}，偏大")

    # 检查主频
    ratio = dom_freq / expected_freq
    if 0.8 < ratio < 1.2:
        print(f"  [PASS] 主频 {dom_freq:.3f} Hz ≈ 预期 {expected_freq} Hz")
        print(f"         内部校准信号正确！SPI/FIFO/转换/SD 链路正常。")
    elif dom_freq > 8 and dom_freq < 12:
        print(f"  [WARN] 主频 {dom_freq:.3f} Hz ≈ 9.7Hz (旧问题频率)")
        print(f"         校准配置可能未被正确写入，或 SPI/FIFO 解析有误")
    elif 2 < dom_freq < 8:
        print(f"  [WARN] 主频 {dom_freq:.3f} Hz，与预期 1Hz 和旧问题 9.7Hz 都不同")
        print(f"         FCAL 位可能被错误解析")
    else:
        print(f"  [WARN] 主频 {dom_freq:.3f} Hz，与预期 {expected_freq} Hz 不符")
        print(f"         检查校准配置或时钟")

    # 综合结论
    print(f"\n--- 综合结论 ---")
    is_periodic = (len(np.where(fft_mag[1:] > dom_mag * 0.3)[0]) <= 3)
    has_expected_freq = 0.8 < ratio < 1.2

    if is_periodic and has_expected_freq:
        print(f"  [PASS] 波形周期性明显，主频正确")
        print(f"  建议: 内部校准链路正常，下一步检查外部 ECGP/ECGN 输入。")
    elif is_periodic and not has_expected_freq:
        print(f"  [NOTE] 有周期性但频率不符合预期 ({dom_freq:.2f} Hz)")
        print(f"  建议: 检查 FCAL 配置和时钟分频设置。")
    elif not is_periodic and has_expected_freq:
        print(f"  [PARTIAL] 主频正确但周期性不明显")
        print(f"  建议: 检查信号幅值和噪声水平。")
    else:
        print(f"  [FAIL] 未检测到有效的校准信号")
        print(f"  建议: 检查 SPI 通信、寄存器写入回读、校准使能位。")

    # ====== 绘图 ======
    # 时域
    plt.figure(figsize=(14, 5))
    plt.subplot(2, 1, 1)
    plt.plot(ts, ecg, linewidth=0.6, color='steelblue')
    plt.xlabel("Time (s)")
    plt.ylabel("ECG raw")
    plt.title(f"MAX30003 Internal Calibration - {os.path.basename(fpath)} (dom_freq={dom_freq:.3f} Hz)")
    plt.grid(True, alpha=0.3)
    plt.xlim(0, min(duration, 10))  # 前 10 秒

    # 频域
    plt.subplot(2, 1, 2)
    plt.plot(freqs, fft_mag, linewidth=0.6, color='red')
    plt.xlim(0, 20)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("Magnitude")
    plt.title("FFT Spectrum")
    plt.grid(True, alpha=0.3)

    plt.tight_layout()

    # 保存图片
    out_dir = os.path.dirname(fpath) if os.path.dirname(fpath) else "."
    fig_path = os.path.join(out_dir, "calibration_check.png")
    plt.savefig(fig_path, dpi=150)
    print(f"\n  图片保存: {fig_path}")

    plt.show()


if __name__ == "__main__":
    main()
