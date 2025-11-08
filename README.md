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
| M5 – software bad arc, sweeps | `scratch/ring6-step4.cc` | Fault demo: `./build/scratch/ns3.46.1-ring6-step4-default --mode=global --badArc=4,5 --faultOnMean=4 --faultOffMean=8 --faultStart=10 --simTime=120 --count=600 --rate=5`<br>`./build/scratch/ns3.46.1-ring6-step4-default --mode=local --badArc=4,5 --faultOnMean=4 --faultOffMean=8 --faultStart=10 --simTime=120 --count=600 --rate=5`<br>Sweep: `./ring6_sweep.py --ns3-dir references/ns-3-dev --log-dir sweeps --tinfo 0 4 --modes global local --bad-arc 2,3 --fault-on-mean 4 --fault-off-mean 8 --fault-start 10 --count 3 --rate 2` |
| M6 – snapshot epoch sweep | `scratch/ring6-step4.cc` | `./ring6_sweep.py --ns3-dir references/ns-3-dev --log-dir sweeps --tinfo 0 0.5 1 2 4 8 --modes global local --bad-arc 4,5 --fault-on-mean 4 --fault-off-mean 8 --fault-start 10 --count 6 --rate 1` |
| M7 – finite buffers + waste/drops | `scratch/ring6-step4.cc` | `./ring6_sweep.py --ns3-dir references/ns-3-dev --log-dir sweeps --tinfo 0 0.5 1 2 4 8 --modes global local --bad-arc 4,5 --fault-on-mean 4 --fault-off-mean 8 --fault-start 10 --count 6 --rate 1 --buffer 2` |

Each run now logs `TX`, `DROP`, and `DELIVERED` events per packet ID, and ends with `RESULT mode=… delivered=… ttlDrops=… noRouteDrops=… blockedTx=… queueDrops=… wasteTx=…` so automation can capture buffer drops and wasted transmissions. Use `--simTime=<seconds>` to keep the simulator running beyond the default `count/rate` window (e.g., for multi-minute studies).

## Automating Sweeps & CSV Output

Use `ring6_sweep.py` to sweep controller modes, snapshot cadences, and fault settings. Example (fixed fault window):

```bash
./ring6_sweep.py \
  --ns3-dir references/ns-3-dev \
  --log-dir sweeps \
  --modes global local \
  --tinfo 0 0.5 1 2 4 8 \
  --bad-arc 2,3 --fault-on-mean 20 --fault-off-mean 60 --fault-start 0 \
  --buffer 2 \
  --rate 5 --count 10
```

For long-running randomized outages, add `--sim-time` while setting the exponential outage parameters:

```bash
./ring6_sweep.py \
  --ns3-dir references/ns-3-dev \
  --log-dir sweeps/random \
  --modes global local \
  --tinfo 2 4 6 8 \
  --bad-arc 4,5 \
  --fault-on-mean 4 --fault-off-mean 8 \
  --fault-start 10 \
  --sim-time 180 \
  --rate 10 --count 200 --buffer 1 \
  --trials 5 --rng-run-start 1
```

The script emits one log per case under `sweeps/` and writes `sweeps/ring6_sweep_results.csv`, which is ready for plotting (e.g., pandas/matplotlib). Use `--csv <path>` to override the output location if needed.

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
- WasteTx plots automatically overlay the “TooSlowToKnow Limit” curve  
  `1133 · [1 − exp(-0.5 · (Tinfo / 2.5))]` so you can compare controller waste directly against the theoretical bound.

## Makefile Shortcuts

Common randomized studies can be kicked off via `make`:

- `make random_tinfo_avg` – run the 600 s, 5 k-packet randomized Tinfo sweep (arc 4→5, five trials, `rng-run-start=101`), producing `sweeps/random_tinfo_avg/ring6_random_tinfo_avg.csv`.
- `make random_multiarc_avg` – run the four 600 s multi-arc sweeps (arcs 1→2, 0→1, 3→4, 5→0 with seeds 201/301/401/501) and merge them into `sweeps/random_multiarc_avg/ring6_random_multiarc_avg.csv`.
- `make plots` – regenerate the confidence-band figures for both datasets (requires the CSVs above).

Run `make help` to see the available targets.

## Published Averaged Datasets

- **Randomized Tinfo grid (arc 4→5):** `sweeps/random_tinfo_avg/ring6_random_tinfo_avg.csv` averages five randomized-fault trials per controller across `Tinfo ∈ {0, 0.5, 1, 2, 4, 8, 12, 16, 24, 32}`. Reproduce with

  ```bash
  ./ring6_sweep.py --ns3-dir references/ns-3-dev \
    --log-dir sweeps/random_tinfo_avg \
    --modes global local \
    --tinfo 0 0.5 1 2 4 8 12 16 24 32 \
    --bad-arc 4,5 \
    --fault-on-mean 4 --fault-off-mean 6 --fault-start 10 \
    --sim-time 600 --count 5000 --rate 10 --buffer 1 \
    --trials 5 --rng-run-start 101 \
    --csv sweeps/random_tinfo_avg/ring6_random_tinfo_avg.csv
  ```
  The longer `simTime`/`count` combination reduces stochastic variance (e.g., Global’s delivered std dev drops to ~11 packets at `Tinfo=0.5`, ~35 at `Tinfo=2`) while Local stays at 5000 deliveries with effectively zero spread. Use the `*Std` columns to draw confidence bands directly from the CSV.
- **Multiple bad arcs:** `sweeps/random_multiarc_avg/ring6_random_multiarc_avg.csv` concatenates four per-arc sweeps (`1→2`, `0→1`, `3→4`, `5→0`) each captured with the same randomized fault model, `Tinfo ∈ {0, 2, 4, 8, 16}`, and five trials. Individual logs live under `sweeps/random_multiarc_avg/<arc>/`. Use the same command template as above while swapping `--bad-arc` and `--rng-run-start`; the combined CSV keeps the `badArc` column so downstream tooling can split traces per fault location.
  ```bash
  ./ring6_sweep.py --ns3-dir references/ns-3-dev \
    --log-dir sweeps/random_multiarc_avg/arcXY \
    --modes global local \
    --tinfo 0 2 4 8 16 \
    --bad-arc X,Y \
    --fault-on-mean 4 --fault-off-mean 6 --fault-start 10 \
    --sim-time 600 --count 5000 --rate 10 --buffer 1 \
    --trials 5 --rng-run-start <seed> \
    --csv sweeps/random_multiarc_avg/ring6_random_arcXY_avg.csv
  ```
  (Run once each for arcs 1→2, 0→1, 3→4, 5→0 with seeds 201/301/401/501, then concatenate into `ring6_random_multiarc_avg.csv`.) The extended runs shrink the confidence regions and better expose steady-state behaviors (e.g., Global averages ≈4.6 k deliveries vs the TooSlowToKnow limit at high `Tinfo`, while Local stays at 5 k except when the sink ingress is blocked).

Key takeaways visible in these datasets:

- Snapshot-Global now averages ≈4.9 k deliveries at `Tinfo ≤ 1` but slumps to ≈3.0–3.8 k once `Tinfo ≥ 12`, while Local remains pegged at 5000 deliveries with zero waste throughout the 600 s randomized runs.
- On the critical arc `3→4`, Local continues to reroute counter-clockwise (5000 deliveries every time) whereas Global ranges from ≈3.0 k (no snapshots) to ≈4.7 k (tight snapshots) with narrow ±σ bands thanks to the longer simulations.
- Blocking the sink ingress (`5→0`) still hurts both controllers equally: the averaged CSV shows ≈3.1 k deliveries with ±0.3 k spread for every `Tinfo`, so the plots’ confidence bands overlap completely and highlight the lack of any viable alternate route.

## Metrics & Logging

- `TX seq=… from=X to=Y queueDepth=Z` – emitted whenever a node dequeues a packet and calls `McpsDataRequest`.
- `DROP seq=… reason=…` – admission drops (queue full), blocked transmissions, TTL expiry, and no-route situations all log via this helper and feed the `queueDrops`/`wasteTx` counters.
- `DELIVERED seq=… src=…` – sink deliveries (also increments `delivered`).
- `queueDrops` counts packets refused because a node’s buffer (configured via `--B`/`--buffer`) was full.
- `wasteTx` sums every `TX` for packets that eventually drop, making it easy to spot wasted airtime in Snapshot-Global runs.
- Fault scheduling:
  - Randomized outages are the supported path: provide `--faultOnMean` / `--faultOffMean` (and optional `--faultStart`) whenever `--badArc` is set to block an arc using exponential ON/OFF durations until `simTime`. Omit `--badArc` to keep the ring healthy.
- The sweep helper exposes `--sim-time`, `--fault-on-mean`, `--fault-off-mean`, and `--fault-start` so CSV rows record how you drove the randomized experiments.
- Use `--trials N --rng-run-start M` to run each configuration multiple times (with incrementing `RngRun`/fault streams) and write mean/stddev columns for every metric, allowing easy comparison of averaged behaviors.

## Fault / Link-Outage Model

- Set `--bad-arc i,j` to identify the directed edge you want to disrupt; omit the flag to keep the ring healthy.
- Randomized outages use an alternating-renewal process:
  - `--fault-on-mean` and `--fault-off-mean` define the exponential ON/OFF durations (mean seconds blocked vs healthy).
  - `--fault-start` delays the first random toggle (default 0 s).
  - The simulator keeps drawing ON/OFF intervals until `--sim-time` elapses, so long runs see multiple outages per experiment.
- Combine `--trials` with `--rng-run-start` to average over multiple seeds. The CSV picks up the mean/std columns automatically, and `plot_sweep.py --confidence-style band` turns them into visual confidence intervals.

## Logs & Artifacts

- Per-milestone “baseline” logs live alongside their scratch sources (e.g., `references/ns-3-dev/scratch/ring6-step3.log`).
- Fault demo logs (`ring6-step4-*-demo.log`) illustrate Milestone 5 behavior and can be compared directly.
- Sweep logs plus CSV live under `sweeps/` by default.

## Next Steps

- Add EWMA-based ETX gating or BCP-style queue differentials so Snapshot-Global can respond to dynamic link quality instead of fixed parents.
- Explore longer sweeps (multiple bad arcs, varied `--count/--rate`) to stress the finite buffers and show `queueDrops` more prominently in the CSV/plots.
- Use `--trials` + `--rng-run-start` (and randomized faults) to publish averaged datasets with confidence bands for each controller.
- Optional: persist example CSV/PNG artifacts in the repo to make documentation reproducible without rerunning ns-3.
