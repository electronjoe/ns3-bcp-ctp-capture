# Handoff Summary

This document captures the current state of the ns-3 ring topology work, the artifacts produced so far, and clear next actions so future-you (or another contributor) can resume without re-discovery.

---

## Repository Overview

- `plan/PLAN.md` – canonical milestone plan. Updated through Milestone 5 with tests/visualization commands.
- `README.md` – user-facing guide for running milestones, sweeps, and plots.
- `references/ns-3-docs/` – curated RST docs (lr-wpan, mobility, propagation, packets, etc.).
- `references/ns-3-dev/` – full ns-3-dev clone with scratch programs/logs (note: separate Git repo).
- `ring6_sweep.py` – automation script that sweeps controller modes/snapshot periods, writes logs + CSV.
- `plot_sweep.py` – generates Matplotlib plots from the sweep CSV.
- `sweeps/` – contains latest sweep logs, CSV (`ring6_sweep_results.csv`), and plots (`sweeps/plots/*.png`).

Everything else in `references/ns-3-dev` (src/, scripts, etc.) is upstream ns-3.

---

## Current Milestone Status

| Milestone | Status | Key files/tests |
|-----------|--------|-----------------|
| M0 | Complete (pre-existing build) | `./ns3 build` |
| M1 | Complete | `references/ns-3-dev/scratch/ring6-step1.cc`, tests described in README/plan |
| M2 | Complete | `references/ns-3-dev/scratch/ring6-step2.cc`, logs `ring6-step2.log` |
| M3 | Complete | `references/ns-3-dev/scratch/ring6-step3.cc`, log `ring6-step3.log` |
| M4 | Complete | `references/ns-3-dev/scratch/ring6-step4.cc` (controllers/metrics) |
| M5 | Complete | Fault demo logs (`ring6-step4-global-demo.log`, `ring6-step4-local-demo.log`), sweep CSV/plots |
| M6 | Not started | Next action |
| M7 | Not started | Pending |

Milestone 5 deliverables:
- CLI: `--mode`, `--badArc`, `--badOn`, `--badOff`, `--Tinfo`, `--count`, `--rate`, etc.
- `RingHeader` now carries a direction byte for local reroute awareness.
- Metrics recorded per run (`delivered`, `ttlDrops`, `noRouteDrops`, `blockedTx`) and printed via `RESULT …`.
- `ring6_sweep.py` + `plot_sweep.py` provide automation and visualization.

---

## Recent Tests & Outputs

All commands run from `references/ns-3-dev` unless noted:

```
./ns3 build scratch/ring6-step1
./build/scratch/ns3.46.1-ring6-step1-default [--extended=1]
./ns3 build scratch/ring6-step2 && ./build/scratch/ns3.46.1-ring6-step2-default
./ns3 build scratch/ring6-step3 && ./build/scratch/ns3.46.1-ring6-step3-default
./ns3 build scratch/ring6-step4
./build/scratch/ns3.46.1-ring6-step4-default --mode=global
./build/scratch/ns3.46.1-ring6-step4-default --mode=local
./build/scratch/ns3.46.1-ring6-step4-default --mode=global --badArc=4,5 --badOn=2 --badOff=8 --count=6 --rate=1
./build/scratch/ns3.46.1-ring6-step4-default --mode=local --badArc=4,5 --badOn=2 --badOff=8 --count=6 --rate=1
```

Sweeps/plots run from repo root:

```
./ring6_sweep.py --ns3-dir references/ns-3-dev --log-dir sweeps --tinfo 0 4 --modes global local --bad-arc 2,3 --bad-on 2 --bad-off 8 --count 3 --rate 2
python3 plot_sweep.py --csv sweeps/ring6_sweep_results.csv --out-dir sweeps/plots
```

Artifacts worth keeping:
- `references/ns-3-dev/scratch/ring6-step4-global-demo.log` : shows Global controller stuck when arc (4→5) is blocked.
- `references/ns-3-dev/scratch/ring6-step4-local-demo.log` : shows Local controller rerouting/continuing deliveries.
- `sweeps/ring6_sweep_results.csv` : sample sweep across `Tinfo` values.
- `sweeps/plots/delivered_vs_tinfo.png`, `blockedTx_vs_tinfo.png`.

---

## Where Work Paused / Next Steps

We’ve completed Milestone 5 (fault emulation + metrics). Remaining tasks per PLAN:

1. **Milestone 6 – Snapshot Epochs (`T_info`):**
   - The code already supports `--Tinfo`; currently we manually set it via CLI or sweeps.
   - Next: run more expansive sweeps (e.g., `Tinfo = 0, 10, 20, 40, 80, 160`) with the same bad arc to show how Global suffers as `T_info` grows.
   - Extend `plot_sweep.py` to include TTL drops/no-route drops if we observe those in more extreme tests.
   - Possibly add a README section for interpreting the `T_info` vs. delivered plots.

2. **Milestone 7 – Finite buffers and waste/drops:**
   - Add an application-level queue (cap B) so `SendToNeighbor` or the source enqueues before calling `McpsDataRequest`.
   - Tag packets with IDs and log `TX`, `DROP`, `DELIVERED`.
   - Extend metrics struct and `RESULT` line to include waste/drops.
   - Update `ring6_sweep.py`/`plot_sweep.py` to include new columns/plots.

3. **General polish:**
   - Consider capturing multiple `(badArc, badOn, badOff)` scenarios in sweeps to illustrate more complex dynamics.
   - Add CLI flags for TTL/test duration if needed for longer runs (Plan currently uses `count` + `rate`).
   - Optional: check in example CSV/plots for documentation.

4. **Git considerations:**
   - `references/ns-3-dev` is a nested Git repo. Commit scratch changes/logs there if you want history.
   - Top-level repo currently tracks plan/README/scripts/sweeps; you may want to commit these (`git add plan/ PLAN`, `README.md`, `ring6_sweep.py`, `plot_sweep.py`, `sweeps/`).

---

## Tips for Resuming

- Before coding, re-run the validation commands (especially `./ns3 build scratch/ring6-step4` and the two fault demos) to ensure nothing regressed.
- For plotting, `plot_sweep.py` depends on Matplotlib; ensure it’s installed in whatever environment you resume from (`pip install matplotlib` if needed).
- When adding new metrics, update both `ring6_sweep.py` (CSV headers) and `plot_sweep.py` (support for new columns). Make sure the `RESULT` line prints the metrics exactly as parsed.
- Keep logs targeted: we’re now generating many under `references/ns-3-dev/scratch/` and `sweeps/`. Consider a cleanup script later.
- If tests take longer, add `--simTime`/`--seed` CLI flags to `ring6-step4.cc` to control duration/determinism.

---

## Quick Reference: Key Files

| File | Purpose |
|------|---------|
| `references/ns-3-dev/scratch/ring6-step1.cc` | Two-node lr-wpan baseline (Milestone 1). |
| `references/ns-3-dev/scratch/ring6-step2.cc` | Six-node neighbor pings (Milestone 2). |
| `references/ns-3-dev/scratch/ring6-step3.cc` | Clockwise forwarder + CLI (Milestone 3). |
| `references/ns-3-dev/scratch/ring6-step4.cc` | Controller modes, fault toggles, metrics (Milestones 4–5 groundwork). |
| `ring6_sweep.py` | Controller/fault sweep automation + CSV output. |
| `plot_sweep.py` | Matplotlib plotting from CSV (generates PNGs). |
| `sweeps/ring6_sweep_results.csv` | Latest sweep data. |
| `sweeps/plots/*.png` | Latest delivered/blocked plots vs `Tinfo`. |
| `plan/PLAN.md` | Step-by-step plan + status/tests. |
| `README.md` | Quickstart, CLI references, and plotting instructions. |

---

Feel free to add more to `HANDOFF.md` as work progresses so future transitions remain smooth. Good luck on Milestones 6 and 7!
