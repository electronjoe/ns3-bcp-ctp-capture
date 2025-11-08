#!/usr/bin/env python3
"""
Plot utils for ring6_sweep_results.csv.
"""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt


def load_rows(csv_path):
    rows = []
    with csv_path.open() as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            row["Tinfo"] = float(row["Tinfo"])
            row["delivered"] = float(row["delivered"]) if row["delivered"] != "" else 0.0
            row["blockedTx"] = float(row["blockedTx"]) if row["blockedTx"] != "" else 0.0
            rows.append(row)
    return rows


def plot_metric(rows, metric, out_path):
    modes = sorted(set(r["mode"] for r in rows))
    plt.figure()
    for mode in modes:
        xs = []
        ys = []
        for row in rows:
            if row["mode"] != mode:
                continue
            xs.append(row["Tinfo"])
            ys.append(row[metric])
        plt.plot(xs, ys, marker="o", label=f"{mode} ({metric})")
    plt.xlabel("Tinfo (s)")
    plt.ylabel(metric)
    plt.title(f"{metric} vs Tinfo")
    plt.legend()
    plt.grid(True, linestyle="--", alpha=0.4)
    plt.tight_layout()
    plt.savefig(out_path)
    plt.close()


def main():
    parser = argparse.ArgumentParser(description="Plot ring6 sweep CSV metrics.")
    parser.add_argument("--csv", default="sweeps/ring6_sweep_results.csv", type=Path)
    parser.add_argument("--out-dir", default="sweeps/plots", type=Path)
    parser.add_argument("--metrics", nargs="+", default=["delivered", "blockedTx"])
    args = parser.parse_args()

    csv_path = args.csv.resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    rows = load_rows(csv_path)
    if not rows:
        raise SystemExit("No rows found in CSV.")

    for metric in args.metrics:
        out_file = out_dir / f"{metric}_vs_tinfo.png"
        plot_metric(rows, metric, out_file)
        print(f"Wrote {out_file}")


if __name__ == "__main__":
    main()
