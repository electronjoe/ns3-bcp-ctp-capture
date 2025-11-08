#!/usr/bin/env python3
"""
Plot utils for ring6_sweep_results.csv.
"""

import argparse
import csv
import math
from pathlib import Path

import matplotlib.pyplot as plt

DEFAULT_METRICS = ["delivered", "blockedTx", "queueDrops", "wasteTx"]


def parse_filters(raw_filters):
    filters = []
    for expr in raw_filters:
        if "=" not in expr:
            raise ValueError(f"Invalid filter '{expr}'. Expected format key=value.")
        key, value = expr.split("=", 1)
        filters.append((key.strip(), value.strip()))
    return filters


def row_matches_filters(row, filters):
    for key, expected in filters:
        if key not in row:
            return False
        actual = row[key]
        if isinstance(actual, (int, float)):
            try:
                expected_val = float(expected)
            except ValueError:
                return False
            if actual != expected_val:
                return False
        else:
            if str(actual) != expected:
                return False
    return True


def load_rows(csv_path):
    rows = []
    with csv_path.open() as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            if row.get("Tinfo", "") != "":
                row["Tinfo"] = float(row["Tinfo"])
            for field in DEFAULT_METRICS + ["ttlDrops", "noRouteDrops"]:
                if field in row and row[field] != "":
                    row[field] = float(row[field])
            for key in list(row.keys()):
                if key.endswith("Std") and row[key] != "":
                    row[key] = float(row[key])
            rows.append(row)
    return rows


def plot_metric(rows, metric, out_path, group_by, confidence_style):
    groups = {}
    for row in rows:
        key = row.get(group_by, "all")
        groups.setdefault(key, []).append(row)

    if not groups:
        raise SystemExit("No groups to plot after filtering.")

    plt.figure()
    for group_key in sorted(groups):
        series = sorted(groups[group_key], key=lambda r: r.get("Tinfo", 0.0))
        xs = [r.get("Tinfo", 0.0) for r in series]
        ys = [r.get(metric, 0.0) for r in series]
        label = f"{group_key}"
        (line,) = plt.plot(xs, ys, marker="o", label=label)

        std_field = f"{metric}Std"
        stds = []
        has_std = False
        for r in series:
            std_val = r.get(std_field)
            if std_val is None or std_val == "":
                std_val = 0.0
            else:
                has_std = has_std or std_val > 0
            stds.append(std_val)

        if has_std and confidence_style != "none":
            color = line.get_color()
            if confidence_style == "band":
                lower = [max(0.0, y - s) for y, s in zip(ys, stds)]
                upper = [y + s for y, s in zip(ys, stds)]
                plt.fill_between(xs, lower, upper, color=color, alpha=0.2)
            elif confidence_style == "errorbar":
                plt.errorbar(xs, ys, yerr=stds, fmt="none", ecolor=color, alpha=0.6)

    if metric == "wasteTx":
        xs_limit = sorted({row.get("Tinfo", 0.0) for row in rows})
        ys_limit = [1133.0 * (1.0 - math.exp(-0.5 * (x / 2.5))) for x in xs_limit]
        if xs_limit:
            plt.plot(xs_limit,
                     ys_limit,
                     linestyle="--",
                     color="black",
                     linewidth=1.2,
                     label="TooSlowToKnow Limit")

    plt.xlabel("Tinfo (s)")
    plt.ylabel(metric)
    plt.title(f"{metric} vs Tinfo (grouped by {group_by})")
    plt.legend()
    plt.grid(True, linestyle="--", alpha=0.4)
    plt.tight_layout()
    plt.savefig(out_path)
    plt.close()


def main():
    parser = argparse.ArgumentParser(description="Plot ring6 sweep CSV metrics.")
    parser.add_argument("--csv", default="sweeps/ring6_sweep_results.csv", type=Path)
    parser.add_argument("--out-dir", default="sweeps/plots", type=Path)
    parser.add_argument(
        "--metrics",
        nargs="+",
        default=DEFAULT_METRICS,
    )
    parser.add_argument(
        "--group-by",
        default="mode",
        help="CSV column used to split lines (default: mode).",
    )
    parser.add_argument(
        "--filter",
        action="append",
        default=[],
        metavar="COL=VALUE",
        help="Filter rows before plotting; can be specified multiple times.",
    )
    parser.add_argument(
        "--confidence-style",
        choices=["band", "errorbar", "none"],
        default="band",
        help="How to visualize std columns when present.",
    )
    args = parser.parse_args()

    csv_path = args.csv.resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    rows = load_rows(csv_path)
    if args.filter:
        filters = parse_filters(args.filter)
        rows = [row for row in rows if row_matches_filters(row, filters)]

    if not rows:
        raise SystemExit("No rows found in CSV after filtering.")

    for metric in args.metrics:
        out_file = out_dir / f"{metric}_vs_tinfo.png"
        plot_metric(rows, metric, out_file, args.group_by, args.confidence_style)
        print(f"Wrote {out_file}")


if __name__ == "__main__":
    main()
