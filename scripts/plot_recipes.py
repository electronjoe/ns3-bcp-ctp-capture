#!/usr/bin/env python3
# scripts/plot_recipes.py
import csv
import sys
import matplotlib.pyplot as plt

def plot_per_vs_sinr(csv_path, out_path):
    xs, ys = [], []
    with open(csv_path) as f:
        r = csv.DictReader(f)
        for row in r:
            xs.append(float(row["sinr_db"]))
            ys.append(float(row["per"]))
    xs2, ys2 = zip(*sorted(zip(xs, ys)))
    plt.figure()
    plt.semilogy(xs2, ys2, marker="o")
    plt.xlabel("SINR (dB)")
    plt.ylabel("PER (log scale)")
    plt.grid(True, which="both", ls=":")
    plt.savefig(out_path, bbox_inches="tight")
    print(f"Saved plot to {out_path}")

def plot_capture_toggle(csv_path, out_path):
    xs, ys = [], []
    with open(csv_path) as f:
        r = csv.DictReader(f)
        for row in r:
            xs.append(float(row["delta_db"]))
            ys.append(float(row["per"]))
    xs2, ys2 = zip(*sorted(zip(xs, ys)))
    plt.figure()
    plt.plot(xs2, ys2, marker="o")
    plt.xlabel("Interferer–Desired Δ (dB)")
    plt.ylabel("PER of desired link")
    plt.grid(True, ls=":")
    plt.savefig(out_path, bbox_inches="tight")
    print(f"Saved plot to {out_path}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: plot_recipes.py <per|cap> [--in CSV] [--out PNG]")
        print("  or:  plot_recipes.py <per|cap> <csv_path> <out_path>")
        sys.exit(1)

    mode = sys.argv[1]

    # Support both --in/--out flags and positional args
    if "--in" in sys.argv:
        idx_in = sys.argv.index("--in")
        csv_path = sys.argv[idx_in + 1]
        idx_out = sys.argv.index("--out")
        out_path = sys.argv[idx_out + 1]
    elif len(sys.argv) >= 4:
        csv_path = sys.argv[2]
        out_path = sys.argv[3]
    else:
        print("Error: missing CSV or output path")
        sys.exit(1)

    if mode == "per":
        plot_per_vs_sinr(csv_path, out_path)
    elif mode == "cap":
        plot_capture_toggle(csv_path, out_path)
    else:
        print(f"Unknown mode: {mode}")
        sys.exit(1)
