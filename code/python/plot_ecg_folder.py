"""
ECG CSV 文件夹批量可视化工具
用法: python python/plot_ecg_folder.py <文件夹路径>
示例: python python/plot_ecg_folder.py Data/
      python python/plot_ecg_folder.py .
"""

import os
import sys
import glob
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np


def read_ecg_csv(filepath):
    """读取单条 ECG CSV 文件，返回 (df, metadata)"""
    try:
        df = pd.read_csv(filepath)
    except Exception as e:
        print(f"  [跳过] 读取失败: {e}")
        return None, None

    # 检查必要列
    if "ecg" not in df.columns or "timestamp_ms" not in df.columns:
        print(f"  [跳过] 缺少 ecg 或 timestamp_ms 列")
        return None, None

    # 计算基本统计
    n = len(df)
    t_start = df["timestamp_ms"].iloc[0]
    t_end = df["timestamp_ms"].iloc[-1]
    duration_s = (t_end - t_start) / 1000.0 if t_end > t_start else 0.0
    fs = n / duration_s if duration_s > 0 else 0.0

    metadata = {
        "file": os.path.basename(filepath),
        "samples": n,
        "duration_s": duration_s,
        "sample_rate": fs,
        "ecg_min": df["ecg"].min(),
        "ecg_max": df["ecg"].max(),
        "ecg_mean": df["ecg"].mean(),
        "ecg_std": df["ecg"].std(),
    }
    return df, metadata


def plot_ecg(ax, df, meta, color="steelblue"):
    """在指定 axes 上绘制 ECG 波形"""
    t = (df["timestamp_ms"] - df["timestamp_ms"].iloc[0]) / 1000.0  # 秒
    ax.plot(t, df["ecg"], color=color, linewidth=0.4, alpha=0.85)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("ECG (raw)")
    ax.set_title(f"{meta['file']}  —  {meta['samples']} samples, "
                 f"{meta['duration_s']:.1f}s @ {meta['sample_rate']:.0f} Hz")
    ax.grid(True, alpha=0.3)
    ax.margins(x=0.01)


def print_summary(meta_list, output_folder):
    """打印所有文件的汇总表格"""
    print(f"\n{'='*90}")
    print(f"{'File':<20} {'Samples':<10} {'Duration(s)':<12} "
          f"{'Fs(Hz)':<8} {'Min':<8} {'Max':<8} {'Mean':<8} {'Std':<8}")
    print(f"{'-'*90}")

    for m in meta_list:
        print(f"{m['file']:<20} {m['samples']:<10} {m['duration_s']:<12.1f} "
              f"{m['sample_rate']:<8.0f} {m['ecg_min']:<8.0f} {m['ecg_max']:<8.0f} "
              f"{m['ecg_mean']:<8.1f} {m['ecg_std']:<8.1f}")

    print(f"{'='*90}")
    print(f"共 {len(meta_list)} 个文件, 输出到: {output_folder}")


def main():
    # 解析参数
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    folder = sys.argv[1]
    if not os.path.isdir(folder):
        print(f"[错误] 文件夹不存在: {folder}")
        sys.exit(1)

    # 搜索所有 ecg_*.csv 文件
    csv_files = sorted(glob.glob(os.path.join(folder, "ecg_*.csv")))
    if not csv_files:
        print(f"[错误] 在 {folder} 中未找到 ecg_*.csv 文件")
        sys.exit(1)

    print(f"找到 {len(csv_files)} 个 ECG CSV 文件:\n")
    for f in csv_files:
        print(f"  {f}")

    # 读取所有文件
    results = []
    for f in csv_files:
        df, meta = read_ecg_csv(f)
        if df is not None:
            results.append((df, meta))
            print(f"  [OK] {meta['file']}: {meta['samples']} samples, "
                  f"{meta['duration_s']:.1f}s, {meta['sample_rate']:.0f} Hz")

    if not results:
        print("\n没有可绘制的有效文件")
        sys.exit(1)

    # 打印汇总
    metas = [r[1] for r in results]
    output_dir = os.path.join(folder, "ecg_plots")
    os.makedirs(output_dir, exist_ok=True)
    print_summary(metas, output_dir)

    # ---- 绘图 ----
    n_files = len(results)
    n_cols = min(2, n_files)
    n_rows = (n_files + n_cols - 1) // n_cols

    # 1. 逐文件单独图
    for i, (df, meta) in enumerate(results):
        fig, ax = plt.subplots(1, 1, figsize=(12, 3))
        plot_ecg(ax, df, meta)
        plt.tight_layout()
        out_path = os.path.join(output_dir, f"ecg_{i+1:03d}_{meta['file'].replace('.csv','')}.png")
        fig.savefig(out_path, dpi=150)
        plt.close(fig)
        print(f"  已保存: {out_path}")

    # 2. 组合图（所有文件叠加在同一坐标轴）
    fig, axes = plt.subplots(n_rows, n_cols, figsize=(14, 3 * n_rows), squeeze=False)
    colors = plt.cm.tab10(np.linspace(0, 1, n_files))

    for i, (df, meta) in enumerate(results):
        row, col = divmod(i, n_cols)
        ax = axes[row][col]
        plot_ecg(ax, df, meta, color=colors[i])

    # 留空白处隐藏
    for i in range(n_files, n_rows * n_cols):
        row, col = divmod(i, n_cols)
        axes[row][col].axis("off")

    plt.tight_layout()
    overview_path = os.path.join(output_dir, "overview.png")
    fig.savefig(overview_path, dpi=150)
    plt.close(fig)
    print(f"  已保存: {overview_path}")

    # 3. 显示第一个文件（交互窗口）
    print(f"\n显示第一个文件波形窗口...")
    fig, ax = plt.subplots(1, 1, figsize=(14, 4))
    plot_ecg(ax, results[0][0], results[0][1])
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
