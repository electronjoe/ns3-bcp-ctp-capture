# ns-3 Ring6 Sandbox

This repository hosts a reproducible workflow for rapidly iterating on a 6-node LR-WPAN ring inside `ns-3-dev`.  Each milestone lives in `references/ns-3-dev/scratch/` (`ring6-step1.cc` … `ring6-step4.cc`) and builds on the previous one to showcase global vs. local routing policies, software-controlled “bad arcs,” and controller sweeps.

## Repository Layout

- `plan/PLAN.md` – source-of-truth milestone plan with live status/tests.
- `references/ns-3-dev/` – full upstream `ns-3-dev` checkout; scratch programs and logs live in `references/ns-3-dev/scratch/`.
- `references/ns-3-docs/` – curated reStructuredText docs (lr-wpan, mobility, propagation, etc.) for offline reference.
- `ring6_sweep.py` – Python helper to sweep controller modes / snapshot cadences and emit logs + CSV summaries.
- `sweeps/` – default output directory for sweep logs and `ring6_sweep_results.csv`.

> **Note:** `references/ns-3-dev` is its own Git repository. Commit ns-3 changes there, or keep it as a nested checkout and track only top-level artifacts here.

## Ring Topology & Traffic Defaults

- Six lr-wpan nodes (`kNumNodes=6`) sit on a circle of radius 15 m. Node indices follow the ring clockwise, so each node has exactly two physical neighbors: `(i+1) mod 6` clockwise and `(i-1+6) mod 6` counter-clockwise.
- Unless overridden via CLI, node 3 acts as the source and injects packets toward sink node 0 starting at `t = 1 s`. Packets march around the ring hop-by-hop; the controller (global snapshot vs local myopic) simply chooses which neighbor to use at each relay.
- MAC-16 addresses are tied to the node index (`00:01` … `00:06`), so logs/plots map naturally back to the ring positions.

## Prerequisites

- GCC/Clang toolchain, Python 3.8+, and dependencies required by upstream `ns-3-dev`.
- The repository already contains a configured/buildable `ns-3-dev`; run from the top level:

```bash
cd references/ns-3-dev
./ns3 build
```

## Milestone Programs & Tests

| Milestone | Scratch Program | Test Commands |
|-----------|-----------------|---------------|
| M1 – two-node baseline | `scratch/ring6-step1.cc` | `./ns3 build scratch/ring6-step1`<br>`./build/scratch/ns3.46.1-ring6-step1-default` (add `--extended=1` to test 64-bit addressing) |
| M2 – six-node neighbor ring | `scratch/ring6-step2.cc` | `./ns3 build scratch/ring6-step2`<br>`./build/scratch/ns3.46.1-ring6-step2-default` |
| M3 – clockwise forwarder | `scratch/ring6-step3.cc` | `./ns3 build scratch/ring6-step3`<br>`./build/scratch/ns3.46.1-ring6-step3-default` |
| M4 – controller toggle + metrics | `scratch/ring6-step4.cc` | `./ns3 build scratch/ring6-step4`<br>`./build/scratch/ns3.46.1-ring6-step4-default --mode=global`<br>`./build/scratch/ns3.46.1-ring6-step4-default --mode=local` |
| M5 – software bad arc, sweeps | `scratch/ring6-step4.cc` | Fault demo: `./build/scratch/ns3.46.1-ring6-step4-default --mode=global --badArc=4,5 --badOn=2 --badOff=8 --count=6 --rate=1`<br>`./build/scratch/ns3.46.1-ring6-step4-default --mode=local --badArc=4,5 --badOn=2 --badOff=8 --count=6 --rate=1`<br>Sweep: `./ring6_sweep.py --ns3-dir references/ns-3-dev --log-dir sweeps --tinfo 0 4 --modes global local --bad-arc 2,3 --bad-on 2 --bad-off 8 --count 3 --rate 2` |
| M6 – snapshot epoch sweep | `scratch/ring6-step4.cc` | `./ring6_sweep.py --ns3-dir references/ns-3-dev --log-dir sweeps --tinfo 0 0.5 1 2 4 8 --modes global local --bad-arc 4,5 --bad-on 2 --bad-off 8 --count 6 --rate 1` |
| M7 – finite buffers + waste/drops | `scratch/ring6-step4.cc` | `./ring6_sweep.py --ns3-dir references/ns-3-dev --log-dir sweeps --tinfo 0 0.5 1 2 4 8 --modes global local --bad-arc 4,5 --bad-on 2 --bad-off 8 --count 6 --rate 1 --buffer 2` |

Each run now logs `TX`, `DROP`, and `DELIVERED` events per packet ID, and ends with `RESULT mode=… delivered=… ttlDrops=… noRouteDrops=… blockedTx=… queueDrops=… wasteTx=…` so automation can capture buffer drops and wasted transmissions. Use `--simTime=<seconds>` to keep the simulator running beyond the default `count/rate` window (e.g., for multi-minute studies).

## Automating Sweeps & CSV Output

Use `ring6_sweep.py` to sweep controller modes, snapshot cadences, and fault settings. Example (fixed fault window):

```bash
./ring6_sweep.py \
  --ns3-dir references/ns-3-dev \
  --log-dir sweeps \
  --modes global local \
  --tinfo 0 0.5 1 2 4 8 \
  --bad-arc 2,3 --bad-on 20 --bad-off 60 \
  --buffer 2 \
  --rate 5 --count 10
```

For long-running randomized outages, add `--sim-time` and switch to `--fault-mode random`:

```bash
./ring6_sweep.py \
  --ns3-dir references/ns-3-dev \
  --log-dir sweeps/random \
  --modes global local \
  --tinfo 2 4 6 8 \
  --bad-arc 4,5 \
  --fault-mode random \
  --fault-on-mean 4 --fault-off-mean 8 \
  --fault-start 10 --fault-stream 5 \
  --sim-time 180 \
  --rate 10 --count 200 --buffer 1 \
  --trials 5 --rng-run-start 1
```

The script emits one log per case under `sweeps/` and writes `sweeps/ring6_sweep_results.csv`, which is ready for plotting (e.g., pandas/matplotlib). Use `--dry-run` to preview commands or `--csv <path>` to override the output location.

## Visualizing Sweep Results

`plot_sweep.py` takes the CSV and produces per-metric figures (Matplotlib required):

```bash
python3 plot_sweep.py \
  --csv sweeps/ring6_sweep_results.csv \
  --out-dir sweeps/plots \
  --metrics delivered blockedTx ttlDrops
```

Generated PNGs (default: `sweeps/plots/delivered_vs_tinfo.png`, `blockedTx_vs_tinfo.png`, …) plot each controller’s metric against `Tinfo`.  Re-run after each sweep to refresh the charts.
Additional CLI options help when working with the averaged/randomized datasets:

- `--group-by <column>` (default `mode`) splits each line/marker; try `--group-by badArc` when plotting the combined multi-arc CSV.
- `--filter COL=VALUE` (repeatable) slices the CSV before plotting, e.g. `--filter badArc=3,4`.
- `--confidence-style {band,errorbar,none}` controls how `*Std` columns are rendered; the default fills a confidence band around each series to visualize ±σ.

## Published Averaged Datasets

- **Randomized Tinfo grid (arc 4→5):** `sweeps/random_tinfo_avg/ring6_random_tinfo_avg.csv` averages five randomized-fault trials per controller across `Tinfo ∈ {0, 0.5, 1, 2, 4, 8, 12, 16, 24, 32}`. Reproduce with

  ```bash
  ./ring6_sweep.py --ns3-dir references/ns-3-dev \
    --log-dir sweeps/random_tinfo_avg \
    --modes global local \
    --tinfo 0 0.5 1 2 4 8 12 16 24 32 \
    --bad-arc 4,5 \
    --fault-mode random --fault-on-mean 4 --fault-off-mean 6 --fault-start 10 \
    --sim-time 180 --count 300 --rate 10 --buffer 1 \
    --trials 5 --rng-run-start 101 \
    --csv sweeps/random_tinfo_avg/ring6_random_tinfo_avg.csv
  ```
  The CSV now carries `*Std` columns for every metric, so plotting libraries can draw confidence bands directly from e.g., `deliveredStd` vs `Tinfo`.
- **Multiple bad arcs:** `sweeps/random_multiarc_avg/ring6_random_multiarc_avg.csv` concatenates four per-arc sweeps (`1→2`, `0→1`, `3→4`, `5→0`) each captured with the same randomized fault model, `Tinfo ∈ {0, 2, 4, 8, 16}`, and five trials. Individual logs live under `sweeps/random_multiarc_avg/<arc>/`. Use the same command template as above while swapping `--bad-arc` and `--rng-run-start`; the combined CSV keeps the `badArc` column so downstream tooling can split traces per fault location.

Key takeaways visible in these datasets:

- Snapshot-Global collapses once `Tinfo` exceeds the mean fault interval (delivered average drops to ~238 ± 41 packets at `Tinfo ≥ 16`), while Local stays pegged at 300 deliveries with zero waste for the same random outages.
- When the blocked arc sits on the only clockwise path (e.g., `3→4`), Local maintains full delivery through the counter-clockwise detour whereas Global’s waste climbs linearly with `Tinfo`.
- Blocking the sink’s ingress (`5→0`) harms both controllers because neither has an alternate hop; the averaged CSV still quantifies the shared collapse (≈228 ± 34 deliveries, ≈72 TTL drops) and enables you to visualize overlapping confidence bands.

## Metrics & Logging

- `TX seq=… from=X to=Y queueDepth=Z` – emitted whenever a node dequeues a packet and calls `McpsDataRequest`.
- `DROP seq=… reason=…` – admission drops (queue full), blocked transmissions, TTL expiry, and no-route situations all log via this helper and feed the `queueDrops`/`wasteTx` counters.
- `DELIVERED seq=… src=…` – sink deliveries (also increments `delivered`).
- `queueDrops` counts packets refused because a node’s buffer (configured via `--B`/`--buffer`) was full.
- `wasteTx` sums every `TX` for packets that eventually drop, making it easy to spot wasted airtime in Snapshot-Global runs.
- Fault scheduling:
  - `--faultMode=fixed` (default) reuses a single `--badOn/--badOff` window.
  - `--faultMode=random` plus `--faultOnMean` / `--faultOffMean` (and optional `--faultStart`, `--faultStream`) repeatedly toggles the blocked arc using exponential ON/OFF durations until `simTime`. Pair with `--simTime` and higher `--count/--rate` to capture averaged behavior.
- The sweep helper exposes `--sim-time`, `--fault-mode`, `--fault-on-mean`, `--fault-off-mean`, `--fault-start`, and `--fault-stream` so CSV rows record how you drove the randomized experiments.
- Use `--trials N --rng-run-start M` to run each configuration multiple times (with incrementing `RngRun`/fault streams) and write mean/stddev columns for every metric, allowing easy comparison of averaged behaviors.

## Fault / Link-Outage Model

- Deterministic outages: set `--bad-arc i,j` to block transmissions from node `i` to node `j` between `--bad-on` and `--bad-off`. This models a single scheduled fault window and is useful for plotting controller recovery vs snapshot cadence.
- Probabilistic outages: switch to `--fault-mode random` to replace the single window with an alternating renewal process:
  - `--fault-on-mean` and `--fault-off-mean` define exponential ON/OFF durations (mean seconds spent blocked vs healthy).
  - `--fault-start` delays the first random toggle (default 0 s).
  - `--fault-stream` seeds the random draws; each trial bumps the stream so sweeps can average over independent schedules.
  - The simulator keeps drawing ON/OFF intervals until `--sim-time` elapses, so long runs see multiple outages per experiment.
- Combining `--trials`, `--rng-run-start`, and the random fault mode yields CSV rows with both means and standard deviations, which `plot_sweep.py --confidence-style band` turns into visual confidence intervals.

## Logs & Artifacts

- Per-milestone “baseline” logs live alongside their scratch sources (e.g., `references/ns-3-dev/scratch/ring6-step3.log`).
- Fault demo logs (`ring6-step4-*-demo.log`) illustrate Milestone 5 behavior and can be compared directly.
- Sweep logs plus CSV live under `sweeps/` by default.

## Next Steps

- Add EWMA-based ETX gating or BCP-style queue differentials so Snapshot-Global can respond to dynamic link quality instead of fixed parents.
- Explore longer sweeps (multiple bad arcs, varied `--count/--rate`) to stress the finite buffers and show `queueDrops` more prominently in the CSV/plots.
- Use `--trials` + `--rng-run-start` (and randomized faults) to publish averaged datasets with confidence bands for each controller.
- Optional: persist example CSV/PNG artifacts in the repo to make documentation reproducible without rerunning ns-3.
