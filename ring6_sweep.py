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


def run_case(ns3_dir, run_args, log_path, dry_run=False):
    cmd = ["./ns3", "run", build_command(run_args)]
    if dry_run:
        print(f"[dry-run] {' '.join(cmd)}")
        return {"status": "dry-run", "log": log_path, "result": None}

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
    parser.add_argument("--payload", type=int, default=40)
    parser.add_argument("--source", type=int, default=3)
    parser.add_argument("--sink", type=int, default=0)
    parser.add_argument("--bad-arc", default=None, help="Format i,j (optional)")
    parser.add_argument("--bad-on", type=float, default=20.0)
    parser.add_argument("--bad-off", type=float, default=60.0)
    parser.add_argument("--ttl", type=int, default=6)
    parser.add_argument("--buffer", type=int, default=5, help="per-node buffer capacity (B)")
    parser.add_argument("--sim-time", type=float, default=None, help="Override --simTime for ns-3 run")
    parser.add_argument(
        "--fault-mode",
        choices=["fixed", "random"],
        default="fixed",
        help="fixed window (default) or randomized fault epochs",
    )
    parser.add_argument("--fault-on-mean", type=float, default=None, help="Mean fault ON duration (random mode)")
    parser.add_argument("--fault-off-mean", type=float, default=None, help="Mean healthy duration (random mode)")
    parser.add_argument("--fault-start", type=float, default=None, help="Time to begin randomized toggling")
    parser.add_argument("--fault-stream", type=int, default=None, help="RNG stream for randomized faults")
    parser.add_argument("--trials", type=int, default=1, help="Repeat each configuration this many times")
    parser.add_argument("--rng-run-start", type=int, default=1, help="Base RngRun passed to ns-3 for trials")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--csv", default=None, help="CSV output path (default: <log-dir>/ring6_sweep_results.csv)")

    args = parser.parse_args()

    ns3_dir = args.ns3_dir.resolve()
    log_dir = args.log_dir.resolve()

    sweep_results = []
    rows = []

    if args.fault_mode == "random" and not args.bad_arc:
        parser.error("--fault-mode random requires --bad-arc to be specified")
    if args.trials <= 0:
        parser.error("--trials must be >= 1")

    fault_stream_base = args.fault_stream if args.fault_stream is not None else 1

    for mode in args.modes:
        for tinfo in args.tinfo:
            run_args = {
                "mode": mode,
                "Tinfo": tinfo,
                "rate": args.rate,
                "count": args.count,
                "payload": args.payload,
                "source": args.source,
                "sink": args.sink,
                "ttl": args.ttl,
                "B": args.buffer,
            }
            if args.sim_time is not None:
                run_args["simTime"] = args.sim_time
            if args.fault_mode:
                run_args["faultMode"] = args.fault_mode
            if args.bad_arc:
                run_args["badArc"] = args.bad_arc
                run_args["badOn"] = args.bad_on
                run_args["badOff"] = args.bad_off
            if args.fault_mode == "random":
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
                if args.fault_mode == "random":
                    trial_run_args["faultStream"] = fault_stream_base + trial

                log_suffix = f"-trial{trial + 1}" if args.trials > 1 else ""
                log_name = f"ring6-step4-mode{mode}-tinfo{tinfo:g}{log_suffix}.log"
                log_path = log_dir / log_name

                result = run_case(ns3_dir, trial_run_args, log_path, dry_run=args.dry_run)
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
                "payload": args.payload,
                "source": args.source,
                "sink": args.sink,
                "ttl": args.ttl,
                "B": args.buffer,
                "simTime": args.sim_time if args.sim_time is not None else "",
                "faultMode": args.fault_mode,
                "faultOnMean": args.fault_on_mean if args.fault_on_mean is not None else "",
                "faultOffMean": args.fault_off_mean if args.fault_off_mean is not None else "",
                "faultStart": args.fault_start if args.fault_start is not None else "",
                "faultStream": args.fault_stream if args.fault_stream is not None else (fault_stream_base if args.fault_mode == "random" else ""),
                "badArc": args.bad_arc or "",
                "badOn": args.bad_on if args.bad_arc else "",
                "badOff": args.bad_off if args.bad_arc else "",
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
        elif entry["status"] != "dry-run":
            print("  (no RESULT line found)")

    if rows:
        csv_path = pathlib.Path(args.csv).resolve() if args.csv else (log_dir / "ring6_sweep_results.csv")
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        headers = [
            "mode",
            "Tinfo",
            "rate",
            "count",
            "payload",
            "source",
            "sink",
            "ttl",
            "B",
            "simTime",
            "faultMode",
            "faultOnMean",
            "faultOffMean",
            "faultStart",
            "faultStream",
            "badArc",
            "badOn",
            "badOff",
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

    if any(e["status"] != "ok" and e["status"] != "dry-run" for e in sweep_results):
        sys.exit(1)


if __name__ == "__main__":
    main()
