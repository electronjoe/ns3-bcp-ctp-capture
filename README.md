# ns-3 Ring6 Sandbox

This repository hosts a reproducible workflow for rapidly iterating on a 6-node LR-WPAN ring inside `ns-3-dev`.  Each milestone lives in `references/ns-3-dev/scratch/` (`ring6-step1.cc` … `ring6-step4.cc`) and builds on the previous one to showcase global vs. local routing policies, software-controlled “bad arcs,” and controller sweeps.

## Repository Layout

- `plan/PLAN.md` – source-of-truth milestone plan with live status/tests.
- `references/ns-3-dev/` – full upstream `ns-3-dev` checkout; scratch programs and logs live in `references/ns-3-dev/scratch/`.
- `references/ns-3-docs/` – curated reStructuredText docs (lr-wpan, mobility, propagation, etc.) for offline reference.
- `ring6_sweep.py` – Python helper to sweep controller modes / snapshot cadences and emit logs + CSV summaries.
- `sweeps/` – default output directory for sweep logs and `ring6_sweep_results.csv`.

> **Note:** `references/ns-3-dev` is its own Git repository. Commit ns-3 changes there, or keep it as a nested checkout and track only top-level artifacts here.

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

Each run now logs `TX`, `DROP`, and `DELIVERED` events per packet ID, and ends with `RESULT mode=… delivered=… ttlDrops=… noRouteDrops=… blockedTx=… queueDrops=… wasteTx=…` so automation can capture buffer drops and wasted transmissions.

## Automating Sweeps & CSV Output

Use `ring6_sweep.py` to sweep controller modes, snapshot periods, and fault settings. Example:

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

## Metrics & Logging

- `TX seq=… from=X to=Y queueDepth=Z` – emitted whenever a node dequeues a packet and calls `McpsDataRequest`.
- `DROP seq=… reason=…` – admission drops (queue full), blocked transmissions, TTL expiry, and no-route situations all log via this helper and feed the `queueDrops`/`wasteTx` counters.
- `DELIVERED seq=… src=…` – sink deliveries (also increments `delivered`).
- `queueDrops` counts packets refused because a node’s buffer (configured via `--B`/`--buffer`) was full.
- `wasteTx` sums every `TX` for packets that eventually drop, making it easy to spot wasted airtime in Snapshot-Global runs.

## Logs & Artifacts

- Per-milestone “baseline” logs live alongside their scratch sources (e.g., `references/ns-3-dev/scratch/ring6-step3.log`).
- Fault demo logs (`ring6-step4-*-demo.log`) illustrate Milestone 5 behavior and can be compared directly.
- Sweep logs plus CSV live under `sweeps/` by default.

## Next Steps

- Add EWMA-based ETX gating or BCP-style queue differentials so Snapshot-Global can respond to dynamic link quality instead of fixed parents.
- Explore longer sweeps (multiple bad arcs, varied `--count/--rate`) to stress the finite buffers and show `queueDrops` more prominently in the CSV/plots.
- Optional: persist example CSV/PNG artifacts in the repo to make documentation reproducible without rerunning ns-3.
