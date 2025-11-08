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
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--csv", default=None, help="CSV output path (default: <log-dir>/ring6_sweep_results.csv)")

    args = parser.parse_args()

    ns3_dir = args.ns3_dir.resolve()
    log_dir = args.log_dir.resolve()

    sweep_results = []
    rows = []

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
            }
            if args.bad_arc:
                run_args["badArc"] = args.bad_arc
                run_args["badOn"] = args.bad_on
                run_args["badOff"] = args.bad_off

            log_name = f"ring6-step4-mode{mode}-tinfo{tinfo:g}.log"
            log_path = log_dir / log_name

            result = run_case(ns3_dir, run_args, log_path, dry_run=args.dry_run)
            result["args"] = deepcopy(run_args)
            sweep_results.append(result)

            if not args.dry_run:
                row = {
                    "mode": mode,
                    "Tinfo": tinfo,
                    "rate": args.rate,
                    "count": args.count,
                    "payload": args.payload,
                    "source": args.source,
                    "sink": args.sink,
                    "ttl": args.ttl,
                    "badArc": args.bad_arc or "",
                    "badOn": args.bad_on if args.bad_arc else "",
                    "badOff": args.bad_off if args.bad_arc else "",
                    "status": result["status"],
                    "log": str(log_path),
                }
                metrics = result.get("metrics") or {}
                row.update({
                    "delivered": metrics.get("delivered", ""),
                    "ttlDrops": metrics.get("ttlDrops", ""),
                    "noRouteDrops": metrics.get("noRouteDrops", ""),
                    "blockedTx": metrics.get("blockedTx", ""),
                })
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
            "badArc",
            "badOn",
            "badOff",
            "delivered",
            "ttlDrops",
            "noRouteDrops",
            "blockedTx",
            "status",
            "log",
        ]
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
