"""
ECG CSV 通用信号分析工具 — 增强版
- 支持文件夹批量分析或单个文件
- 50Hz 陷波滤波
- seq 连续性检查、统计、FFT、THD
- 显示波形图 + 频谱图
- 报告保存为 txt + json 汇总

用法:
  python python/analyze_ecg.py <文件夹路径>
  python python/analyze_ecg.py <单个csv文件>
"""
import os, sys, glob, json
import numpy as np
import pandas as pd
import matplotlib
import matplotlib.pyplot as plt

# 50Hz 陷波滤波
_scipy_available = False
try:
    from scipy import signal as scipy_signal
    _scipy_available = True
except ImportError:
    pass


def notch_filter_50hz(data, fs):
    """50Hz IIR 陷波滤波，抑制工频干扰"""
    if not _scipy_available:
        return data
    if fs <= 0:
        return data
    try:
        b, a = scipy_signal.iirnotch(50.0, 30.0, fs)
        return scipy_signal.filtfilt(b, a, data)
    except Exception:
        return data


def remove_baseline_wander(data, fs):
    """去除基线漂移 (0.5Hz 高通, 零相位)"""
    if not _scipy_available:
        return data
    if fs <= 0:
        return data
    try:
        b, a = scipy_signal.butter(3, 0.5, btype='high', fs=fs)
        return scipy_signal.filtfilt(b, a, data)
    except Exception:
        return data


def lowpass_40hz(data, fs):
    """40Hz Butterworth 低通滤波 (5阶, 零相位)"""
    if not _scipy_available:
        return data
    if fs <= 0:
        return data
    try:
        b, a = scipy_signal.butter(5, 40.0, fs=fs, btype='low')
        return scipy_signal.filtfilt(b, a, data)
    except Exception:
        return data


TRIM_SEC = 5.0   # 切除前 5 秒 (初始稳定期)


def read_csv(filepath):
    try:
        df = pd.read_csv(filepath)
    except Exception as e:
        return None, {"error": str(e)}
    if "ecg" not in df.columns:
        return None, {"error": "缺少 ecg 列"}
    ecg = df["ecg"].values.astype(np.float64)
    n = len(ecg)

    # 解析 seq
    has_seq = "seq" in df.columns
    seq = df["seq"].values.astype(np.int64) if has_seq else np.arange(n, dtype=np.int64)

    # 解析 timestamp_ms
    has_ts = "timestamp_ms" in df.columns
    ts = df["timestamp_ms"].values.astype(np.float64) if has_ts else None

    # MAX30003 固定 512 SPS，不用 timestamp_ms 估算
    fs = 512.0

    meta = {
        "file": os.path.basename(filepath),
        "samples": n,
        "has_seq": has_seq,
        "has_ts": has_ts,
        "fs": fs,
        "path": filepath,
    }
    return ecg, meta, seq, ts


def analyze(signal, fs, seq=None):
    n = len(signal)
    if n == 0:
        return {}

    # 基本统计
    v_min, v_max = float(signal.min()), float(signal.max())
    v_pp = v_max - v_min
    v_mean, v_std = float(signal.mean()), float(signal.std())

    # 百分位数
    pcts = np.percentile(signal, [1, 5, 25, 50, 75, 95, 99])

    # 差分/噪声
    diff = np.diff(signal)
    diff_std = float(np.std(diff))
    noise = diff_std * 0.5
    snr = v_pp / noise if noise > 1e-6 else 0

    # 平坦检测
    flat = np.sum(np.abs(diff) < 1) / max(len(diff), 1) * 100

    # 削顶检测
    clip_h = np.sum(signal >= v_max * 0.99) if abs(v_max) > 0 else 0
    clip_l = np.sum(signal <= v_min * 0.99) if abs(v_min) > 0 else 0
    clip_pct = (clip_h + clip_l) / n * 100

    # 过零率
    zcr = int(np.sum(np.diff(np.signbit(signal))))

    # 基线漂移 (用 1s 移动平均的 STD)
    baseline_win = int(max(1, round(fs)))
    if n > baseline_win * 2:
        baseline = pd.Series(signal).rolling(window=baseline_win, center=True).mean().values
        baseline_std = float(np.nanstd(baseline[baseline_win:-baseline_win] if baseline_win < n//2 else baseline))
    else:
        baseline_std = 0.0

    # 频谱分析
    dom_freqs, dom_mags = [], []
    thd_pct = 0
    if fs > 10 and n > 200:
        windowed = signal * np.hanning(n)
        fft_vals = np.fft.rfft(windowed)
        fft_mag = np.abs(fft_vals) / n
        freqs = np.fft.rfftfreq(n, d=1/fs)
        mask = freqs > 0.3
        if np.any(mask):
            top5 = np.argsort(fft_mag[mask])[-5:][::-1]
            dom_freqs = [round(float(freqs[mask][i]), 3) for i in top5]
            dom_mags = [round(float(fft_mag[mask][i]), 4) for i in top5]

        # THD (总谐波失真): 取 2-5 次谐波 vs 基波
        if dom_freqs:
            f0 = dom_freqs[0]
            harm_indices = []
            for h in range(2, 6):
                idx = np.argmin(np.abs(freqs - f0 * h))
                if idx < len(freqs):
                    harm_indices.append(idx)
            if harm_indices and len(dom_mags) > 0:
                fund = dom_mags[0]
                harm_sum = sum(fft_mag[i] for i in harm_indices)
                thd_pct = round((harm_sum / fund) * 100, 2) if fund > 1e-10 else 0

    # seq 连续性检查
    seq_missing = 0
    if seq is not None and len(seq) > 1:
        seq_diff = np.diff(seq)
        seq_missing = int(np.sum(seq_diff != 1))

    duration_by_seq = 0
    if seq is not None and len(seq) > 1 and fs > 0:
        duration_by_seq = round((seq[-1] - seq[0] + 1) / fs, 3)

    return {
        "min": round(v_min, 1),
        "max": round(v_max, 1),
        "peak_to_peak": round(v_pp, 1),
        "mean": round(v_mean, 1),
        "std": round(v_std, 1),
        "p1": round(float(pcts[0]), 1),
        "p5": round(float(pcts[1]), 1),
        "p25": round(float(pcts[2]), 1),
        "p50": round(float(pcts[3]), 1),
        "p75": round(float(pcts[4]), 1),
        "p95": round(float(pcts[5]), 1),
        "p99": round(float(pcts[6]), 1),
        "noise": round(noise, 1),
        "snr": round(snr, 1),
        "flat_pct": round(flat, 1),
        "clipping_pct": round(clip_pct, 3),
        "zcr": zcr,
        "baseline_std": round(baseline_std, 3),
        "dominant_freqs": dom_freqs,
        "dominant_mags": dom_mags,
        "thd_pct": thd_pct,
        "seq_missing": seq_missing,
        "duration_by_seq": duration_by_seq,
    }


def plot_signal(signal, fs, stats, meta, out_path):
    n = len(signal)
    t_total = n / fs if fs > 0 else 0
    t = np.arange(n) / fs

    # 50Hz 陷波 + 40Hz 低通 + 去基线漂移
    sig_notch = notch_filter_50hz(signal, fs)
    sig_lp = lowpass_40hz(sig_notch, fs)
    sig_filtered = remove_baseline_wander(sig_lp, fs)

    # 导出滤波后 CSV
    csv_fname = os.path.basename(out_path).replace("_plot.png", "_filtered.csv")
    csv_path = os.path.join(os.path.dirname(out_path), csv_fname)
    df_out = pd.DataFrame({
        "time_sec": np.round(t, 4),
        "ecg_raw": signal.astype(np.int16),
        "ecg_filtered": np.round(sig_filtered, 1)
    })
    df_out.to_csv(csv_path, index=False)
    print(f"    滤波CSV: {csv_path}")

    win_10s = min(int(fs * 10), n)

    fig, axes = plt.subplots(3, 1, figsize=(14, 10))

    # ---- 子图1: 原始信号 (前10秒) ----
    ax = axes[0]
    ax.plot(t[:win_10s], signal[:win_10s], linewidth=0.6, color="#1f77b4")
    ax.set_ylabel("ECG raw")
    ax.set_title(f"Raw Signal (first {win_10s/fs:.1f}s)")
    ax.set_ylim(-1000, 1000)
    ax.grid(True, alpha=0.3)

    # ---- 子图2: 滤波 + 去基线后 (前10秒) ----
    ax = axes[1]
    ax.plot(t[:win_10s], sig_filtered[:win_10s], linewidth=0.8, color="#d62728")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("ECG filtered")
    ax.set_title(f"Filtered + Baseline Removed (first {win_10s/fs:.1f}s)")
    ax.set_ylim(-1000, 1000)
    ax.grid(True, alpha=0.3)

    # ---- 子图3: 频谱 (raw vs filtered) ----
    if fs > 10 and n > 200:
        windowed = signal * np.hanning(n)
        fft_mag = np.abs(np.fft.rfft(windowed)) / n
        freqs = np.fft.rfftfreq(n, d=1/fs)

        ax = axes[2]
        ax.plot(freqs, fft_mag, linewidth=0.6, color="#1f77b4", alpha=0.6, label="Raw")

        if sig_filtered is not signal:
            windowed_f = sig_filtered * np.hanning(n)
            fft_mag_f = np.abs(np.fft.rfft(windowed_f)) / n
            ax.plot(freqs, fft_mag_f, linewidth=0.8, color="#d62728", label="50Hz Notch")

        # 标记 50Hz 工频
        ax.axvline(x=50, color="gray", linestyle="--", alpha=0.5, linewidth=0.8)
        ax.annotate("50Hz", xy=(50, ax.get_ylim()[1]*0.9), fontsize=8, color="gray")

        # 标记主频
        if stats["dominant_freqs"]:
            f0 = stats["dominant_freqs"][0]
            ax.axvline(x=f0, color="green", linestyle="--", alpha=0.5, linewidth=0.8)
            ax.annotate(f"{f0}Hz", xy=(f0, ax.get_ylim()[1]*0.8), fontsize=9, color="green")

        ax.set_xlim(0, min(60, freqs[-1]))
        ax.set_xlabel("Frequency (Hz)")
        ax.set_ylabel("Magnitude")
        ax.set_title("FFT Spectrum")
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)
    else:
        ax = axes[2]
        ax.text(0.5, 0.5, "FFT: insufficient data", ha="center", va="center")

    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    print(f"    图片: {out_path}")
    plt.show()


def process_one(fpath, out_dir):
    result = read_csv(fpath)
    if result is None or len(result) < 2:
        return None
    ecg, meta = result[0], result[1]
    seq = result[2] if len(result) > 3 else None

    if meta.get("error"):
        print(f"  [跳过] {os.path.basename(fpath)}: {meta['error']}")
        return None

    fs = meta["fs"]

    # 切除前 TRIM_SEC 秒 (初始稳定期)
    trim_samples = int(fs * TRIM_SEC)
    if len(ecg) > trim_samples:
        ecg = ecg[trim_samples:]
        if seq is not None and len(seq) > trim_samples:
            seq = seq[trim_samples:]
        meta["samples"] = len(ecg)
        print(f"  分析: {meta['file']}  ({meta['samples']} samples @ {fs} Hz, trimmed {TRIM_SEC}s)")
    else:
        print(f"  分析: {meta['file']}  ({meta['samples']} samples @ {fs} Hz)")

    stats = analyze(ecg, fs, seq)

    # 生成报告
    lines = []
    lines.append(f"文件: {meta['file']}")
    lines.append(f"采样数: {meta['samples']}")
    lines.append(f"采样率: {fs} Hz")
    if stats["duration_by_seq"] > 0:
        lines.append(f"时长(seq): {stats['duration_by_seq']}s")
    lines.append("")
    lines.append("--- 基本统计 ---")
    for k in ["min", "max", "peak_to_peak", "mean", "std"]:
        lines.append(f"  {k}: {stats[k]}")
    lines.append("")
    lines.append("--- 百分位数 ---")
    for k in ["p1", "p5", "p25", "p50", "p75", "p95", "p99"]:
        lines.append(f"  {k}: {stats[k]}")
    lines.append("")
    lines.append("--- 噪声/质量 ---")
    lines.append(f"  噪声估计: {stats['noise']}")
    lines.append(f"  SNR: {stats['snr']}")
    lines.append(f"  平坦%: {stats['flat_pct']}")
    lines.append(f"  削顶%: {stats['clipping_pct']}")
    lines.append(f"  过零: {stats['zcr']}")
    lines.append(f"  基线漂移STD: {stats['baseline_std']}")
    lines.append("")
    lines.append("--- 连续性 ---")
    lines.append(f"  seq 缺失数: {stats['seq_missing']}")
    lines.append("")
    lines.append("--- 频谱 ---")
    if stats["dominant_freqs"]:
        for f, m in zip(stats["dominant_freqs"][:5], stats["dominant_mags"][:5]):
            lines.append(f"  {f} Hz  (mag={m})")
        lines.append(f"  THD: {stats['thd_pct']}%")
    else:
        lines.append("  (无)")
    lines.append("")

    # 保存报告
    rname = meta["file"].replace(".csv", "_report.txt")
    rpath = os.path.join(out_dir, rname)
    with open(rpath, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"    报告: {rpath}")

    # 保存图片 + 显示窗口
    pname = meta["file"].replace(".csv", "_plot.png")
    ppath = os.path.join(out_dir, pname)
    if fs > 0:
        plot_signal(ecg, fs, stats, meta, ppath)

    return {
        "file": meta["file"],
        "samples": meta["samples"],
        "fs": fs,
        "duration_by_seq": stats["duration_by_seq"],
        "peak_to_peak": stats["peak_to_peak"],
        "snr": stats["snr"],
        "noise": stats["noise"],
        "flat_pct": stats["flat_pct"],
        "mean": stats["mean"],
        "std": stats["std"],
        "baseline_std": stats["baseline_std"],
        "dominant_freqs": stats["dominant_freqs"][:3],
        "thd_pct": stats["thd_pct"],
        "seq_missing": stats["seq_missing"],
    }


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    target = sys.argv[1]

    if os.path.isdir(target):
        folder = target
        csv_files = sorted(glob.glob(os.path.join(folder, "ecg_*.csv")))
        if not csv_files:
            print(f"[错误] 未找到 ecg_*.csv 文件")
            sys.exit(1)
        out_dir = os.path.join(folder, "ecg_analysis")
    elif os.path.isfile(target):
        csv_files = [target]
        out_dir = os.path.join(os.path.dirname(target) or ".", "ecg_analysis")
    else:
        print(f"[错误] 路径不存在: {target}")
        sys.exit(1)

    os.makedirs(out_dir, exist_ok=True)
    print(f"找到 {len(csv_files)} 个文件\n")

    all_summaries = []
    for fpath in csv_files:
        s = process_one(fpath, out_dir)
        if s:
            all_summaries.append(s)

    if all_summaries:
        print(f"\n--- 汇总 ---")
        for s in all_summaries:
            freqs = ", ".join(str(f) for f in s["dominant_freqs"][:2]) if s["dominant_freqs"] else "-"
            missing = f" miss={s['seq_missing']}" if s["seq_missing"] else ""
            print(f"{s['file']:<22} {s.get('duration_by_seq', 0):<6.1f}s  pp={s['peak_to_peak']:<8}"
                  f" SNR={s['snr']:<6} THD={s['thd_pct']}% 主频={freqs}{missing}")

        summary_path = os.path.join(out_dir, "summary.json")
        with open(summary_path, "w") as f:
            json.dump(all_summaries, f, indent=2)
        print(f"\n汇总: {summary_path}")


if __name__ == "__main__":
    main()
