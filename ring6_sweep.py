#!/usr/bin/env python3
"""
Utility to sweep controller modes / snapshot periods for ring6-step4.
"""

import argparse
import csv
import pathlib
import subprocess
import sys
from copy import deepcopy
from statistics import fmean, pstdev

METRIC_KEYS = ["delivered", "ttlDrops", "noRouteDrops", "blockedTx", "queueDrops", "wasteTx"]


def build_command(args_map):
    """Construct the ns-3 run string."""
    parts = ["scratch/ring6-step4"]
    for key, value in args_map.items():
        if value is None:
            continue
        if isinstance(value, bool):
            continue
        parts.append(f"--{key}={value}")
    return " ".join(parts)


def parse_result_line(line):
    if not line:
        return None
    line = line.strip()
    if not line.startswith("RESULT"):
        return None
    metrics = {}
    tokens = line.split()[1:]
    for token in tokens:
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        try:
            metrics[key] = int(value)
        except ValueError:
            try:
                metrics[key] = float(value)
            except ValueError:
                metrics[key] = value
    return metrics


def run_case(ns3_dir, run_args, log_path):
    cmd = ["./ns3", "run", build_command(run_args)]
    proc = subprocess.run(
        cmd,
        cwd=ns3_dir,
        capture_output=True,
        text=True,
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(proc.stdout + proc.stderr)

    result_line = None
    for line in proc.stdout.splitlines():
        if line.startswith("RESULT "):
            result_line = line.strip()
            break

    return {
        "status": "ok" if proc.returncode == 0 else f"rc={proc.returncode}",
        "log": log_path,
        "result": result_line,
        "metrics": parse_result_line(result_line),
    }


def main():
    parser = argparse.ArgumentParser(description="Sweep ring6-step4 parameters.")
    parser.add_argument("--ns3-dir", default="references/ns-3-dev", type=pathlib.Path)
    parser.add_argument("--log-dir", default="sweeps", type=pathlib.Path)
    parser.add_argument("--modes", nargs="+", default=["global", "local"])
    parser.add_argument("--tinfo", nargs="+", type=float, default=[0.0, 10.0, 40.0])
    parser.add_argument("--rate", type=float, default=5.0)
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--bad-arc", default=None, help="Format i,j (optional)")
    parser.add_argument("--buffer", type=int, default=5, help="per-node buffer capacity (B)")
    parser.add_argument("--sim-time", type=float, default=None, help="Override --simTime for ns-3 run")
    parser.add_argument("--fault-on-mean", type=float, default=None, help="Mean fault ON duration (random mode)")
    parser.add_argument("--fault-off-mean", type=float, default=None, help="Mean healthy duration (random mode)")
    parser.add_argument("--fault-start", type=float, default=None, help="Time to begin randomized toggling")
    parser.add_argument("--trials", type=int, default=1, help="Repeat each configuration this many times")
    parser.add_argument("--rng-run-start", type=int, default=1, help="Base RngRun passed to ns-3 for trials")
    parser.add_argument("--csv", default=None, help="CSV output path (default: <log-dir>/ring6_sweep_results.csv)")

    args = parser.parse_args()

    ns3_dir = args.ns3_dir.resolve()
    log_dir = args.log_dir.resolve()

    sweep_results = []
    rows = []

    if args.trials <= 0:
        parser.error("--trials must be >= 1")

    for mode in args.modes:
        for tinfo in args.tinfo:
            run_args = {
                "mode": mode,
                "Tinfo": tinfo,
                "rate": args.rate,
                "count": args.count,
                "B": args.buffer,
            }
            if args.sim_time is not None:
                run_args["simTime"] = args.sim_time
            if args.bad_arc:
                run_args["badArc"] = args.bad_arc
                if args.fault_on_mean is not None:
                    run_args["faultOnMean"] = args.fault_on_mean
                if args.fault_off_mean is not None:
                    run_args["faultOffMean"] = args.fault_off_mean
                if args.fault_start is not None:
                    run_args["faultStart"] = args.fault_start

            trial_metrics = {key: [] for key in METRIC_KEYS}
            trial_statuses = []
            trial_logs = []

            for trial in range(args.trials):
                trial_run_args = deepcopy(run_args)
                if args.trials > 1:
                    trial_run_args["RngRun"] = args.rng_run_start + trial

                log_suffix = f"-trial{trial + 1}" if args.trials > 1 else ""
                log_name = f"ring6-step4-mode{mode}-tinfo{tinfo:g}{log_suffix}.log"
                log_path = log_dir / log_name

                result = run_case(ns3_dir, trial_run_args, log_path)
                result["args"] = deepcopy(trial_run_args)
                sweep_results.append(result)

                trial_statuses.append(result["status"])
                trial_logs.append(str(log_path))

                metrics = result.get("metrics") or {}
                for key in METRIC_KEYS:
                    value = metrics.get(key)
                    if value in ("", None):
                        value = 0.0
                    trial_metrics[key].append(float(value))

            if args.dry_run:
                continue

            aggregated_status = "ok"
            if any(status != "ok" for status in trial_statuses):
                aggregated_status = ";".join(trial_statuses)

            row = {
                "mode": mode,
                "Tinfo": tinfo,
                "rate": args.rate,
                "count": args.count,
                "B": args.buffer,
                "simTime": args.sim_time if args.sim_time is not None else "",
                "faultOnMean": args.fault_on_mean if args.fault_on_mean is not None else "",
                "faultOffMean": args.fault_off_mean if args.fault_off_mean is not None else "",
                "faultStart": args.fault_start if args.fault_start is not None else "",
                "badArc": args.bad_arc or "",
                "trials": args.trials,
                "status": aggregated_status,
                "log": ";".join(trial_logs) if args.trials > 1 else trial_logs[0],
            }

            for key, values in trial_metrics.items():
                mean_val = fmean(values) if values else 0.0
                row[key] = mean_val
                if args.trials > 1:
                    row[f"{key}Std"] = pstdev(values) if len(values) > 1 else 0.0

            rows.append(row)

    for entry in sweep_results:
        print(f"[{entry['status']}] log={entry['log']}")
        if entry["result"]:
            print(f"  {entry['result']}")
        else:
            print("  (no RESULT line found)")

    if rows:
        csv_path = pathlib.Path(args.csv).resolve() if args.csv else (log_dir / "ring6_sweep_results.csv")
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        headers = [
            "mode",
            "Tinfo",
            "rate",
            "count",
            "B",
            "simTime",
            "faultOnMean",
            "faultOffMean",
            "faultStart",
            "badArc",
            "trials",
            "delivered",
            "ttlDrops",
            "noRouteDrops",
            "blockedTx",
            "queueDrops",
            "wasteTx",
        ]
        if args.trials > 1:
            for key in METRIC_KEYS:
                headers.append(f"{key}Std")
        headers.extend(
            [
                "status",
                "log",
            ]
        )
        with csv_path.open("w", newline="") as csv_file:
            writer = csv.DictWriter(csv_file, fieldnames=headers)
            writer.writeheader()
            for row in rows:
                writer.writerow(row)
        print(f"CSV written to {csv_path}")

    if any(e["status"] != "ok" for e in sweep_results):
        sys.exit(1)


if __name__ == "__main__":
    main()
