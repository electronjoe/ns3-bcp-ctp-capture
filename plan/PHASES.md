# PHASES.md — Hand‑Off Plan for Scenario B (Capture/SINR) and LPF Measurement

This document turns the high‑level plan into **actionable phases** with clear objectives, inputs, deliverables, step‑by‑step tasks, validation checks, and a “definition of done” (DoD) for each phase. It assumes the repository layout from `PLAN.md`.

> **Scope:** ns‑3 simulation harness for 802.15.4 with capture/SINR + Local (DPP/backpressure) vs Snapshot‑Global (+k‑hop veto), metrics & figure generation, and a Python pipeline to compute the **Local Pooling Factor** (LPF, \(\hat{\sigma}\)) from exported topologies.

---

## Roles & Conventions

- **Owner:** Who is expected to execute the phase (default: *Simulation Engineer*).
- **Inputs:** Repos, configs, doc references, seeds.
- **Artifacts:** Files, binaries, CSV/JSON, notebooks/plots produced by the phase.
- **Commands:** Example commands (adapt paths as needed).
- **Validation:** Explicit checks to sign off a phase.
- **DoD:** Definition of done (sign‑off criteria).
- **Risks/Mitigations:** Known pitfalls and how to defuse them.

> **Repo root:** `ns3-bcp-ctp-capture/` unless otherwise stated.

---

## Phase 0 — Repo Bootstrap & Environment

**Owner:** Infra/Build

### Objectives
- Create the repository skeleton and build environment (Docker optional).
- Ensure ns‑3 (lr‑wpan + spectrum + sixlowpan + internet) builds and runs a hello‑world.

### Inputs
- `PLAN.md` repository layout.
- Host with Linux (Ubuntu 22.04+ recommended) or devcontainer.

### Artifacts
- Directory tree, `CMakeLists.txt` (or waf build file), minimal `wpan_capture_sim.cc` stub that compiles and exits.
- (Optional) `Dockerfile` and `devcontainer.json`.

### Tasks
1. **Directory scaffold**
   ```bash
   mkdir -p sim/controllers sim/apps sim/helpers lpf configs scripts out
   touch sim/wpan_capture_sim.cc README.md LICENSE
   ```
2. **Fetch/build ns‑3**
   - Option A (**waf**, canonical):
     ```bash
     git clone https://gitlab.com/nsnam/ns-3-dev.git ns-3
     cd ns-3
     ./ns3 configure --enable-examples --enable-tests
     ./ns3 build
     ```
   - Option B (**CMake external project**): add ns‑3 as a submodule or rely on system install; wire sim target via `ExternalProject_Add`.
3. **Compiler toolchain**
   ```bash
   sudo apt-get update && sudo apt-get install -y \
     build-essential cmake g++ pkg-config python3 python3-venv \
     libgsl-dev libeigen3-dev
   ```
4. **Minimal sim stub**
   - `sim/wpan_capture_sim.cc`: include `<ns3/core-module.h>` and exit `Simulator::Run(); Simulator::Destroy();`.
5. **Build**
   - If embedding into ns‑3 tree: place under `scratch/` and run `./ns3 build && ./ns3 run scratch/wpan_capture_sim`.
   - If standalone CMake: link to ns‑3 libs in `find_package`/`target_link_libraries`.

### Validation
- Build succeeds and a trivial simulation runs, producing nothing but a log line.

### DoD
- Clean build on a fresh clone.
- Documented build steps in `README.md`.

### Risks/Mitigations
- **ns‑3 version drift** → Pin commit in `README.md`.
- **Linker failures** → Prefer `scratch/` integration for first bring‑up.

---

## Phase 1 — PHY Bring‑Up & PER vs SINR Calibration (Capture Threshold)

**Owner:** PHY/Sim

### Objectives
- Use `lr-wpan` + `SpectrumChannel` to realize SINR‑based capture.
- Empirically validate PER vs SINR and capture margin \(x\) dB matches the chosen threshold \(\beta\).

### Inputs
- ns‑3 `lr-wpan` examples; 802.15.4 docs.
- `sim/helpers/channel_export.h/.cc` (create stub).

### Artifacts
- `out/calibration/per_vs_sinr.csv` and plot `per_vs_sinr.png`.
- `configs/capture_medium.yaml` with finalized PHY/MAC params: \(\beta\), capture margin \(x\), noise figure, bandwidth.

### Tasks
1. **Two‑node PER sweep**
   - Place Tx and Rx at varying distances; disable other traffic.
   - Record SINR and PER (ACK success ratio).
   - Export to CSV; generate PER vs SINR plot.
2. **Three‑node capture test**
   - Tx1→Rx1 (desired), Tx2 near Rx1 (interferer).
   - Sweep \(P_2 - P_1\) (dB) around the capture margin \(x\).
   - Verify success when interferer < desired by \(x\) dB fails; success when desired exceeds interferer by ≥ \(x\) dB passes.
3. **Parameterize**
   - Lock in `betaDb`, `captureDb`, `noiseFigureDb`, pathloss model (`LogDistance`), small scale (`Nakagami` m).

### Commands
```bash
./ns3 run "scratch/wpan_capture_sim --mode=calibrate --out=out/calibration --betaDb=7 --captureDb=6"
python3 scripts/plot_recipes.py --per out/calibration/per_vs_sinr.csv
```

### Validation
- PER(SINR) curve has a sharp knee near \(\beta\).
- Capture experiment toggles success around \(x\) dB as expected.

### DoD
- `configs/capture_medium.yaml` finalized; plots saved and referenced.

### Risks/Mitigations
- **PHY error model mismatch** → adjust threshold mapping; use empirical lookup if necessary.

---

## Phase 2 — Dynamics Model (T_dyn) via Jammer & Autocorrelation

**Owner:** Channel/Sim

### Objectives
- Implement environment dynamics with correlation time \(T_{\text{dyn}}\) (on/off jammer or colored noise).
- Verify \(T_{\text{dyn}}\) by measured ETX/SINR autocorrelation.

### Inputs
- `sim/helpers/jammer.h/.cc` skeleton.

### Artifacts
- `out/dynamics/sinr_autocorr.csv`, `etx_autocorr.csv`, plots.
- Config keys: `TdynMs`, `TonMs`, `ToffMs`.

### Tasks
1. **Jammer component**
   - Apply time‑varying pathloss offsets (or inject spectrum noise) to a subset of links.
2. **Compute autocorrelation**
   - Log per‑link SINR & ETX sequences; compute R(τ) and find e‑fold time.
3. **Map to T_dyn**
   - Choose Ton/Toff to achieve target \(T_{\text{dyn}}\).

### Validation
- Estimated e‑fold time ≈ configured \(T_{\text{dyn}}\) ± 10%.

### DoD
- Reusable `Jammer` with CLI knobs; autocorr plots archived.

### Risks/Mitigations
- **Nonstationarity** → run long enough; discard warmup.

---

## Phase 3 — Finite Buffers, Queue Semantics & Waste Accounting

**Owner:** Systems/Sim

### Objectives
- Implement app‑level queues of size \(B\) per node.
- Correctly count **admission drops** vs **downstream drops**, and **wasted transmissions**.

### Inputs
- Paper’s finite‑buffer semantics; `sim/apps/source_app.*`, `sim/apps/sink_app.*`, `sim/helpers/metrics_log.*`.

### Artifacts
- CSVs: `drops.csv`, `waste.csv`, `queues.csv`, `fates.csv`.
- Unit tests: tiny line (3 nodes) asserts \(W_t \ge D_t^{down}\).

### Tasks
1. **Queue implementation**
   - Enqueue arrivals; if full → `D_adm++`.
   - On forward: if next hop’s queue full → `D_down++` and drop here.
2. **Waste registry**
   - For each packet, track upstream TX events.
   - On final drop, add count to `W_t`; on delivery, mark useful.
3. **Sanity unit test**
   - 3‑node line with overload; verify inequality.

### Validation
- In the unit test, `sum(W) >= sum(D_down)` holds; per‑slot invariants preserved.

### DoD
- Metrics logging schema documented; unit tests scripted.

### Risks/Mitigations
- **Double counting TXs** → use packet unique IDs and dedupe.

---

## Phase 4 — ETX Tracker & Feature Normalization

**Owner:** Systems/Sim

### Objectives
- Track per‑link ETX via ACK‑based EWMA, and provide **normalized** features (z‑score) to controllers.
- Log ETX series for both controllers and snapshots.

### Inputs
- `sim/controllers/etx_tracker.*`.

### Artifacts
- `etx_timeseries.csv` per link; rolling mean/variance.

### Tasks
1. **EWMA PDR**
   - Update per ACK/NACK; cap at \(\epsilon\) to avoid division by zero.
2. **Normalization**
   - Maintain running μ, σ; export z‑scored ETX.
3. **Caps & windows**
   - Choose memory α (e.g., 0.1–0.2) and window M for smoothing.

### Validation
- ETX tracks injected PER changes; normalized ETX has mean ≈ 0, std ≈ 1 over a long run.

### DoD
- Controller APIs receive raw and normalized ETX.

### Risks/Mitigations
- **Estimator lag** → report α used; it’s part of realism.

---

## Phase 5 — Local Controller (DPP^λ / Backpressure Gating)

**Owner:** Algorithms/Sim

### Objectives
- Implement Local (per‑slot, fresh features) controller:
  \[
  \theta_{i\to j}(t) = (Q_i - Q_j) - V \cdot \text{ETX}_{i\to j}(t);
  \quad \text{gate if } \theta \ge 0.
  \]
- Optional Tikhonov bias if selecting multiple links in a slot (kept small).

### Inputs
- `sim/controllers/backpressure.*`, ETX tracker.

### Artifacts
- `local_controller_log.csv`: per‑slot selected links, \(\theta\), and gating indicator.

### Tasks
1. **Compute pressures** per neighbor; deterministic tie‑break.
2. **Enqueue send attempts** (1 packet per chosen link per slot).
3. **Trace hooks** to update ETX, queues, and logging.

### Validation
- Under jammer ON, fewer links pass \(\theta \ge 0\); after OFF, they recover (visual “hold breath/exhale”).
- No starvation (observe rotation via queue differentials).

### DoD
- Local delivers stable performance under ε‑slack in baseline runs.

### Risks/Mitigations
- **MAC collisions mask logic** → align attempt windows with slots; average over seeds.

---

## Phase 6 — Snapshot‑Global Controller, AoI/Cadence & k‑Hop Veto

**Owner:** Algorithms/Sim

### Objectives
- Implement Snapshot‑Global:
  - At epoch \(T_m=m\,T_{\text{info}}\): recompute ETX tree (CTP‑like).
  - Between epochs: forward to parent unless **veto** triggers.
- Implement **AoI** tracking and **k‑hop veto** with budget \(\nu\) per slot.

### Inputs
- `sim/controllers/snapshot_global.*`, `sim/helpers/aoi_epoch.*`, `sim/controllers/veto_budget.*`.

### Artifacts
- `snapshot_log.csv`: install times, parents, AoI samples.
- `veto_log.csv`: edit counts, reasons, affected links.

### Tasks
1. **Snapshot install**
   - Freeze ETX; Dijkstra to sink; set parent map.
2. **Forwarding loop**
   - Default to parent; check veto rule (e.g., \(\theta_{i\to parent(i)}<0\) and exists neighbor with \(\theta\ge \tau\)); apply ≤\(\nu\) primitive edits.
3. **AoI**
   - Track age per node; under cadence, verify uniform on \([0,T_{\text{info}}]\) (empirical).

### Validation
- AoI histogram matches uniform; veto counts respect budget; parent changes only at snapshots.

### DoD
- Controller parity: able to run the same traffic/topology as Local; produces metrics.

### Risks/Mitigations
- **Veto makes non‑feasible MAC combos** → veto only changes **forwarding choice**, not simultaneous transmissions; MAC still arbitrates.

---

## Phase 7 — Experiment Harness & CLI (ρ/B/ε Matrices)

**Owner:** Systems/Sim

### Objectives
- One binary (`wpan_capture_sim`) with CLI to run matrices over \(\rho\), buffers \(B\), capture thresholds \(β\), and ε‑slack.
- Seed management and reproducibility.

### Inputs
- `scripts/run_matrix.sh`, `configs/*.yaml`.

### Artifacts
- `out/run_*/metrics/*.csv`, merged `results.parquet`.
- Provenance JSON with params/seeds.

### Tasks
1. **CLI flags** (see `PLAN.md`) and YAML ingest.
2. **Matrix runner**: spawn runs across seeds and grid; enforce timeouts.
3. **Warmup/steady windows**: trim warmup from aggregates.

### Commands
```bash
scripts/run_matrix.sh --matrix configs/matrix_rho.yaml --out out/matrix
python3 scripts/postprocess.py --in out/matrix --out out/agg
```

### Validation
- Re‑running same seed reproduces identical metrics within stochastic tolerance.
- Merged datasets contain all expected combinations.

### DoD
- One‑shot command reproduces the experiment matrix.

### Risks/Mitigations
- **Long runtimes** → parallelize across cores; cap simulation length; store checkpoints.

---

## Phase 8 — Topology/Channel Export & LPF: Conflict Graph, Schedules, \(\hat{\sigma}\)

**Owner:** Algorithms/Python

### Objectives
- Export communication graph and large‑scale gains from ns‑3 runs.
- Build capture‑aware **conflict graph** \(G_c\).
- Estimate **LPF** \(\hat{\sigma}\) by weight sweep comparing GMS vs MWIS.

### Inputs
- `sim/helpers/channel_export.*` (produces `topology.json`).
- Python: `lpf/build_conflict_graph.py`, `lpf/schedule_utils.py`, `lpf/lpf_estimator.py`.

### Artifacts
- `topology.json`, `G_c.graphml`, `lpf_histogram.png`, `lpf.json` (contains `sigma_hat` and summary).

### Tasks
1. **Export topology**
   - Node positions, pathloss gains \(g_{ab}\) (dB), Tx powers, `betaDb`, `captureDb`, noise.
2. **Build \(G_c\)**
   - For each *directed* link pair, add conflict edge if **either** receiver’s SINR < β when both active.
   - (Optional triad check to reduce false independence.)
3. **Enumerate maximal schedules**
   - Randomized greedy (10k–20k sets) with weight perturbations.
4. **Weight sweep**
   - Sample 2k nonnegative weights \(w\). For each:
     - **GMS**: greedy maximal schedule \(s_{GMS}(w)\).
     - **MWIS (ILP)**: optimal \(s_{MW}(w)\).
     - Record ratio \(r(w)=\frac{w^\top s_{GMS}}{w^\top s_{MW}}\).
   - **LPF estimate**: \(\hat{\sigma}=\min_w r(w)\); save histogram, worst‑case \(w^\star\).

### Commands
```bash
python3 scripts/export_topology.py --run out/run_123 --out out/run_123/topology.json
python3 lpf/build_conflict_graph.py --topology out/run_123/topology.json --out out/run_123/Gc.graphml
python3 lpf/lpf_estimator.py --Gc out/run_123/Gc.graphml --weights 2000 --maximal-samples 10000 --out out/run_123/lpf
```

### Validation
- \(\hat{\sigma}\in(0,1]\); histogram sensible; repeating estimator on same graph returns near‑identical \(\hat{\sigma}\).
- Small graph cross‑check: compare ILP vs enumerating **all** independent sets to validate pipeline.

### DoD
- `lpf.json` exists with `sigma_hat` and summary plots.

### Risks/Mitigations
- **ILP scalability** → limit to moderate link counts or use a strong heuristic for MWIS on large graphs; keep ILP for subgraphs or smaller N.

---

## Phase 9 — Figures & Statistical Post‑Processing

**Owner:** Data/Analysis

### Objectives
- Generate the main paper plots with CIs across seeds.
- Annotate capture runs with measured \(\hat{\sigma}\).

### Inputs
- Aggregated Parquet/CSV from Phase 7; `lpf.json` from Phase 8.
- `scripts/plot_recipes.py`.

### Artifacts
- `fig_drops_vs_rho.png`, `fig_waste_vs_rho.png`, `fig_goodput_vs_load.png`, `fig_waste_vs_B.png`, `fig_lpf_hist.png`.

### Tasks
1. **Drops/Waste vs \(\rho\)**
   - For Local vs Global, plot mean ± 95% CI per \(B\).
2. **Goodput vs Load**
   - Trace stability frontier at fixed \(\rho\).
3. **Waste vs \(B\)**
   - Log‑y axis; fit lines to show \(1/B\) vs \(\exp(-\zeta B)\) behavior (qualitative).
4. **LPF captioning**
   - Read `sigma_hat` and annotate figure captions (e.g., “LPF ≈ 0.78”).

### Validation
- Plots reproduce the qualitative trends from the paper.
- CIs tighten with more seeds; no data gaps.

### DoD
- All figures exist under `out/figs/` and are generated by a single script invocation.

### Risks/Mitigations
- **Outlier seeds** → winsorize or increase seed count.

---

## Phase 10 — Cross‑Checks & Acceptance Tests

**Owner:** QA

### Objectives
- Sign‑off against theoretical expectations and internal consistency.

### Inputs
- Results and plots from Phases 7–9.

### Artifacts
- `QA_REPORT.md`: checklist outcomes with screenshots.

### Checks
1. **AoI uniform under cadence**: \(P\{\Delta \ge T_{info}/2\}\approx 0.5\).
2. **Drops/Waste monotonic in \(\rho\)** for Global; Local curves near‑flat.
3. **Waste vs \(B\)**: Global ~ \(1/B\), Local decay ~ exponential (on log‑y).
4. **Unit inequality**: \(W \ge D_{down}\) on the line test.
5. **LPF stability**: \(\hat{\sigma}\) consistent across seeds/topology repeats.

### DoD
- All checks pass; exceptions documented with root cause.

---

## Phase 11 — Reproducibility: Docker, CI, and Seeds

**Owner:** Infra

### Objectives
- Containerize and add basic CI.

### Inputs
- Dockerfile, GitHub Actions (or GitLab CI) config.

### Artifacts
- `docker/` image that runs a small matrix; CI badge; reproducibility note in `README.md`.

### Tasks
1. **Dockerfile**
   - Base on Ubuntu, install ns‑3 deps, Python pkgs; copy repo; run a smoke test.
2. **CI**
   - Job 1: build + run PER calibration; archive plots.
   - Job 2: tiny topology (N=20) matrix subset; archive CSVs.
3. **Seeds**
   - Fix default seed list and document.

### Validation
- CI green; artifacts downloadable.

### DoD
- One command `docker run` reproduces a minimal experiment.

---

## Phase 12 — Variants & Ablations (Optional but Useful)

**Owner:** Research/Sim

### Objectives
- Show robustness to capture threshold, power control, and MAC settings.

### Inputs
- Baseline configs.

### Artifacts
- Variant figures: `fig_variant_beta.png`, `fig_variant_power.png`.

### Tasks
- Repeat main \(\rho\) sweep with `betaDb ∈ {5,7,9}`, `captureDb ∈ {3,6}`.
- Optional near–far power profile {−3, 0} dBm.
- Briefly discuss impact on \(\hat{\sigma}\) and constants.

### DoD
- Variants plotted and summarized in a short appendix note.

---

## Appendix A — Command Reference

### Build & run (waf, ns‑3 tree)
```bash
./ns3 build
./ns3 run "scratch/wpan_capture_sim --seed=1 --nodes=75 --betaDb=7 --captureDb=6 --Tdyn=0.02 --Tinfo=0.12 --buffers=20 --epsilon=0.05 --duration=600s --output=out/run_$(date +%s)"
```

### Run matrix
```bash
scripts/run_matrix.sh --matrix configs/matrix_rho.yaml --out out/matrix
python3 scripts/postprocess.py --in out/matrix --out out/agg
python3 scripts/plot_recipes.py --in out/agg --out out/figs
```

### LPF
```bash
python3 scripts/export_topology.py --run out/run_123 --out out/run_123/topology.json
python3 lpf/build_conflict_graph.py --topology out/run_123/topology.json --out out/run_123/Gc.graphml
python3 lpf/lpf_estimator.py --Gc out/run_123/Gc.graphml --weights 2000 --maximal-samples 10000 --out out/run_123/lpf
```

---

## Appendix B — File/Schema Notes

- **Topology JSON**
  ```json
  {
    "nodes":[{"id":0,"x":0.5,"y":0.5},...],
    "sink":0,
    "tx_power_dbm":{"0":0,"1":0},
    "noise_dbm": -95,
    "beta_db": 7.0,
    "capture_db": 6.0,
    "gains_db":{"0_1": -72.3, "1_0": -72.9, "...": "..."}
  }
  ```
- **Metrics CSV (per run)**
  - `time_ms, drops_adm, drops_down, waste, goodput, qlen_mean, etx_mean, aoi_mean, offered_load`

---

## Appendix C — Risk Register (Selected)

- **PHY capture realism**: If PER vs SINR differs from datasheet, use empirical lookup table mapped into the error model.
- **LPF approximation**: Pairwise conflict graphs under‑approximate SINR feasibility for >2 links; document LPF as a *lower bound* and optionally run the triad check.
- **MWIS ILP runtime**: For N=100 nodes the link graph may be large; cap to subgraphs or use heuristics; keep exact ILP for small/mid graphs.

---

**End of PHASES.md**
