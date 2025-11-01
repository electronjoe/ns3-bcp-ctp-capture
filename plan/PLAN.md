# PLAN.md — ns-3 Simulation Plan (Capture/SINR Topology) **and** LPF Measurement

**Goal:** Implement the simulation suite for *Scenario B* (capture-aware general topology) from the paper and an **LPF (Local Pooling Factor)** measurement pipeline for the same graphs. The plan is structured so you can (1) reproduce the main figures and (2) quantify the structural constant (\(\hat{\sigma}\)) that underlies the “structural lift” from the line topology to capture/SINR graphs.

---

## 0) Scope & Outcomes

### Deliverables
- **ns-3 experiment harness** (C++): capture-aware IEEE 802.15.4 network with Local (DPP/backpressure) vs Snapshot-Global (+ \(k\)-hop veto) controllers.
- **Topology & channel export** (JSON/CSV) per run: nodes, positions, gains \(g_{ab}\), Tx powers, noise.
- **Metrics exporter** (CSV): drops (admission vs downstream), waste, goodput, queue traces, ETX traces, AoI of snapshots, gating events, offered load.
- **LPF estimator** (Python): builds conflict graph from exported channel matrix + capture rule; computes \(\hat{\sigma}\) via weight-sweep and MWIS vs GMS comparison; optional stability-ramp confirmation.
- **Run scripts** (Bash/Python): experiment matrix, seed management, reproducibility.
- **Docs**: README, config schemas, figure recipes.

### Key Questions Answered
1. **Throughput region contraction in \(\rho\)** (topology-agnostic).
2. **Finite-buffer drop & waste vs \(\rho\)**, with buffer \(B\) dependence, under spatial reuse (capture).
3. **Measured LPF \(\hat{\sigma}\)** of the induced conflict graph; how it shifts constants but preserves the \( (1-e^{-c\rho}) \) and \(1/B\) shapes.

---

## 1) Repository Layout

```
ns3-bcp-ctp-capture/
├─ sim/                       # ns-3 C++ sources
│  ├─ wpan_capture_sim.cc     # main harness (Scenario B)
│  ├─ controllers/
│  │  ├─ backpressure.h/.cc   # Local (DPP^λ) per-slot forwarding controller
│  │  ├─ snapshot_global.h/.cc# Global w/ stale snapshots + k-hop veto
│  │  ├─ etx_tracker.h/.cc    # ETX estimation, feature normalization
│  │  └─ veto_budget.h/.cc    # primitive-edit accounting (ν)
│  ├─ apps/
│  │  ├─ source_app.h/.cc     # traffic source(s): Bernoulli/Poisson, ε-slack tuning
│  │  └─ sink_app.h/.cc       # sink: delivery accounting
│  ├─ helpers/
│  │  ├─ topology.h/.cc       # RGG generator, seed, sink choice
│  │  ├─ channel_export.h/.cc # export gains/powers/noise to JSON/CSV
│  │  ├─ metrics_log.h/.cc    # CSV logging of drops/waste/queues/goodput
│  │  ├─ aoi_epoch.h/.cc      # snapshot cadence (T_info), AoI tracking
│  │  └─ jammer.h/.cc         # interference toggles to realize T_dyn
│  └─ CMakeLists.txt
├─ lpf/                       # Python LPF estimator and utilities
│  ├─ lpf_estimator.py        # core; MWIS (ILP) vs GMS; weight sweep
│  ├─ build_conflict_graph.py # capture-aware conflict graph builder
│  ├─ schedule_utils.py       # enumerators, greedy/maximal schedulers
│  ├─ io_utils.py             # read exported topology/channels
│  ├─ requirements.txt        # ortools (or pulp), numpy, networkx
├─ configs/
│  ├─ base.yaml               # common defaults
│  ├─ capture_medium.yaml     # PHY/MAC params (802.15.4, β, x-dB capture)
│  ├─ matrix_rho.yaml         # ρ sweep (T_info vs T_dyn)
│  ├─ buffers.yaml            # B ∈ {10,20,40,...}
│  └─ traffic.yaml            # offered load & ε-slack grid
├─ scripts/
│  ├─ run_matrix.sh           # run all experiments (calls ns-3 with flags)
│  ├─ postprocess.py          # merges CSVs; computes aggregates & CIs
│  ├─ plot_recipes.py         # generates main figures (matplotlib)
│  ├─ export_topology.py      # CLI wrapper to export channels from a single seed
│  └─ compute_lpf.sh          # calls lpf_estimator with proper args
├─ PLAN.md                    # this file
├─ README.md
└─ LICENSE
```

---

## 2) Environment & Dependencies

- **ns-3**: v3.38+ (tested), modules: `lr-wpan`, `sixlowpan`, `internet`, `applications`, `spectrum`, `flow-monitor`.
- **Compiler**: gcc 10+ or clang 12+; C++17.
- **Python 3.10+** with packages:
  - `ortools` or `pulp` (ILP for MWIS), `networkx`, `numpy`, `scipy`, `pandas`, `matplotlib`.
- **Build**: CMake or ns-3 waf (choose one; CMake recommended in a `contrib`-style tree). Provide `Dockerfile` for reproducibility.

---

## 3) Scenario B Design (Capture/SINR Topology)

### 3.1 Topology
- **Nodes**: N ∈ {50, 75, 100} uniformly at random in [0,1]^2 (RGG). Fixed **sink** at center (or random; record).
- **Radio**: 802.15.4 (`lr-wpan`), single channel.
- **Links (communication reach)**: include neighbor **if** \( P_t g_{tr} / N_0 ≥ \gamma_{\text{rx}} \) (receiver sensitivity). Maintain *directed* links \(t→r\) for all neighbors; store pathloss matrix \(g_{ab}\).
- **Conflict model for PHY**: **SINR + capture**: a concurrent set \(S\) of transmissions succeeds iff each receiver \(r\) has
  \[
  \text{SINR}_r = \frac{P_{t(r)}g_{t(r)r}}{N_0 + \sum_{u\in S\setminus\{t(r)\}} P_{u}g_{ur}} \ge \beta,
  \]
  with \(\beta\) set to reflect the **capture margin** x dB for 802.15.4 (e.g., \(\beta=\beta_0 \cdot 10^{x/10}\)).  
  In ns-3 this is realized by the `SpectrumChannel` + 802.15.4 PHY error model; calibration step below ensures PER vs SINR looks right.

### 3.2 PHY/MAC & Calibration
- **PHY**: `LrWpanPhy` on `SpectrumChannel` with `LogDistancePropagationLoss` + `Nakagami` small-scale fading (m=1–3) or `ConstantSpeedPropagationDelay`.
- **TxPower**: default 0 dBm; optionally two levels {−3, 0} dBm for near–far tests.
- **Noise figure**: e.g., 10 dB; thermal noise from channel bandwidth.
- **MAC**: start with default CSMA/CA; keep **ACKs enabled** for delivery confirmation; set `MaxFrameRetries`.
- **Calibration (one-link test)**:
  1. Place two nodes, sweep distance and interferer power to build PER vs SINR curve; ensure the error model’s threshold maps to \(\beta\) chosen.
  2. Place three nodes (capture test): transmit two concurrently; verify “louder by x dB” capture succeeds with expected probability.

### 3.3 Dynamics: \(T_{\text{dyn}}\) (environment) & \(T_{\text{info}}\) (snapshot cadence)
- **Dynamics \(T_{\text{dyn}}\)**: periodic *interference ON/OFF* or colored noise injection with correlation time \(T_{\text{dyn}}\). Implement with `jammer.h/.cc`:
  - Jammer toggles per-link or per-cluster attenuation (increase pathloss or inject wideband noise) with ON duration \(T_{\text{on}}\), OFF \(T_{\text{off}}\).
  - Choose \(T_{\text{dyn}}\) from channel feature autocorrelation: target e-fold decay at \(T_{\text{dyn}}\).
- **Snapshots \(T_{\text{info}}\)**: install **cadenced** snapshots at \(T_{\text{epoch}}=T_{\text{info}}\) slots (per paper). Dissemination runs continuously, but **install** only at cadence. Record AoI histogram (uniform on \([0,T_{\text{info}}]\) under cadence).
- **Timescale ratio**: \(\rho = T_{\text{info}}/T_{\text{dyn}}\) sweep via \(\{1/2, 1, 2, 4, 6\}\).

### 3.4 Traffic, Queues, Buffers
- **Arrivals**: Bernoulli or Poisson at K sources (default K=⌊0.2N⌋), destined to the sink; per-source rate \(\lambda_s\) tuned to meet a target **ε-slack** (below).
- **Buffers**: per-node finite buffers of size \(B \in \{10,20,40\}\) (packets). Implement **explicit node queues** (not just NetDevice queues) to:
  - **Count admission drops** at ingress when queue full.
  - **Count downstream drops**: when forwarding to next hop, if next-hop queue full, drop and log as downstream overflow.
- **Waste metric**: maintain a per-packet record of all upstream transmissions. When a packet is delivered, mark them “useful”; when dropped, mark “waste” and add their count to \(W_t\).

### 3.5 ETX & feature normalization
- **ETX estimation**: sliding-window ACK-based PER estimate per directed link (e.g., EWMA over last M packets), ETX = 1/PDR. Also compute **z-scored ETX** for normalized feature \(h(S)\) (per paper guidance).
- **Logging**: ETX time series per link; used by both controllers and for snapshot.

---

## 4) Controllers

### 4.1 Local (DPP^λ / Backpressure)
- For each link \(e=(i\to j)\), compute **pressure**
  \[
  \theta_e(t) = \big(Q_i(t)-Q_j(t)\big) - V \cdot \text{ETX}_e(t).
  \]
- **Gating rule** (Lemma in paper): if \(\theta_e(t) \ge 0\), *attempt* transmission on \(e\); else idle. Cap attempts to at most \(R_{\max}\) packets per slot (typically 1).
- **Slotting**: implement a global “tick” every \(\Delta t\) (e.g., 10 ms). On each tick, the backpressure app:
  1) computes \(\theta_e(t)\) for neighbors,  
  2) selects the set of outgoing links with \(\theta_e≥0\) (ties: deterministic order),  
  3) enqueues at most one MAC MSDU per selected link.  
  CSMA/CA and PHY handle actual concurrency & capture; unsuccessful transmissions requeue or count as retry per 802.15.4 MAC.
- **Regularization**: emulate “mild Tikhonov” by penalizing large per-slot send volume: optionally subtract \(\frac{V\lambda}{2}\|x(t)\|_2^2\) when selecting multiple links (if we allow >1 per slot).

### 4.2 Snapshot-Global (+ \(k\)-hop veto)
- **Snapshot build** at install times \(T_m\): freeze ETX \(\widehat{\text{ETX}}\) and compute a CTP-like **minimum-ETX tree** to the sink; fix **parent** per node.
- **Forwarding**: between snapshots, forward *only to parent*.  
- **\(k\)-hop veto with budget \(\nu\)** per slot:
  - Use instantaneous local information within \(k\) hops to apply up to \(\nu\) primitive edits: `cancel(e)` or `activate(e')` (redirect = 2 edits).  
  - Implement a simple local rule: if \(\theta_{(i\to parent(i))}(t) < 0\) **and** there exists neighbor \(j\) with \(\theta_{(i\to j)}(t) ≥ \tau\) (threshold), perform one redirect (counts as 2 edits) subject to \(\nu\). Default: \(k=1\), \(\nu ∈ \{0,2,4\}\).
- **Accounting**: record edits per slot; ensure feasibility (no MAC-level change—this is a **forwarding choice**, not power scheduling).

---

## 5) Metrics & Logging

- **Per time slot \(t\)** (or per Δt window):
  - \(D_t^{adm}\): admission drops.
  - \(D_t^{down}\): downstream-overflow drops.
  - \(W_t\): wasted transmissions (derived by packet fate).
  - **Goodput**: sink receive rate.
  - **Queue lengths** \(Q_i(t)\) (sampled).
  - **ETX** per link, **AoI** (snapshot age) at each node.
  - **Gating indicators**: \(\mathbb{I}\{\theta_e≥0\}\) for a small subset of links (to visualize “hold breath / exhale”).
  - **Offered load** and retry counts.
- **Run-level aggregates**: averages over steady-state window; confidence intervals over 10–20 seeds.

---

## 6) Experiment Matrix

- **Buffers** \(B \in \{10,20,40\}\).
- **ρ sweep**: \(T_{\text{info}}/T_{\text{dyn}} \in \{0.5, 1, 2, 4, 6\}\) by fixing \(T_{\text{dyn}}\) and varying \(T_{\text{info}}\).
- **ε-slack**: pick target slacks \(\varepsilon\in\{0.02,0.05\}\) by calibrating per-source arrival \(\lambda_s\) using a short Local-only warmup to ensure stabilizability.
- **Capture thresholds** \(β\): e.g., \(β \in \{5, 7, 9\}\) dB (effective, after coding), and **capture margin** \(x\) dB in \{3, 6\}.
- **Power profiles**: fixed 0 dBm; optional {−3, 0} dBm mixed.

---

## 7) LPF (\(\hat{\sigma}\)) Measurement Pipeline

### 7.1 Export from ns-3
Each run outputs `topology.json` with:
```json
{
  "nodes": [{"id": i, "x": ..., "y": ...}, ...],
  "sink": s,
  "tx_power_dbm": { "i": P_i, ... },
  "noise_dbm": N0,
  "beta": beta,
  "capture_margin_db": x,
  "gains_db": {"i_j": G_ij, "...": ...}   // large-scale gains (dB)
}
```
Optionally export short-term fading snapshots if you want stochastic LPF; default uses large-scale gains.

### 7.2 Build capture-aware conflict graph \(G_c\)
- For every **directed** link \(\ell=(t\to r)\), define feasibility of **pair** \((\ell_1,\ell_2)\) by checking both receivers’ SINR with **both** transmitters active:
  \[
  \text{SINR}_{r_k} = \frac{P_{t_k}g_{t_k r_k}}{N_0 + P_{t_{3-k}}g_{t_{3-k}r_k}} \ge \beta \quad (k=1,2).
  \]
- Add an **edge** in \(G_c\) if the pair **cannot** co-exist. (This is a **protocol-model** approximation; it yields a conservative graph for LPF.)
- Use `build_conflict_graph.py` to emit NetworkX graph (GEXF/GraphML).

> *Note*: SINR is additive for more than two links; the pairwise graph is an approximation. It aligns with standard LPF practice and is fast/reproducible. Optionally add a “triad check” pass to prune false independence.

### 7.3 Enumerate schedules (independent sets)
- **Maximal schedules \(\mathcal{M}\)**: randomized greedy enumeration to collect M ≈ 5k–20k distinct maximal independent sets:
  - Shuffle link order with weight perturbations
  - Greedy include if not adjacent to chosen set
- Store as 0/1 incidence vectors.

### 7.4 Weight sweep and \(\hat{\sigma}\) estimation
For T ≈ 2000 random **nonnegative** weights \(w\) (Dirichlet or sparse spikes):
1. **Greedy Maximal** per \(w\): run a single greedy pass (descending by \(w\)) to get schedule \(s_{GMS}(w)\), compute \(f_G = w^\top s_{GMS}\).
2. **MW oracle** per \(w\): solve **MWIS** on \(G_c\) (ILP) to get \(s_{MW}(w)\), compute \(f_M = w^\top s_{MW}\).
3. Record ratio \( r(w) = f_G / f_M \).
4. **LPF estimate**: \(\hat{\sigma} = \min_w r(w)\). Report histogram and the worst-case \(w^\star\).

**Optional tighter variant**: allow **time-sharing** among enumerated \(\mathcal{M}\) (LP over `conv(M)`), yielding an upper bound on greedy’s per-slot value relative to the oracle region.

### 7.5 Stability-ramp confirmation (optional)
- Pick worst-case \(w^\star\).
- Map \(w^\star\) to flow demands; ramp offered load in ns-3 until instability under GMS-like behavior is observed; confirm stability under MW-like behavior up to \(\hat{\sigma}\)·boundary (qualitative check).

---

## 8) Implementation Sketches (Key Classes)

### 8.1 `BackpressureApp` (Local controller)
- Maintains per-neighbor queues; enqueue arrivals.
- On each slot:
  - Pull ETX, read neighbor queues \(Q_j\), compute \(\theta_e\).
  - If \(\theta_e≥0\), call `NetDevice->Send()` for one packet toward \(j\)`; else idle.
- Subscribes to MAC/PHY TX/RX/ACK traces to update ETX, queue pops, and “fate” logging.

### 8.2 `SnapshotGlobalApp` (Global + veto)
- Stores parent from last snapshot.
- On each slot:
  - If queue nonempty, send to `parent(i)` unless veto triggers; count veto edits.
- At snapshot install:
  - Run Dijkstra on frozen ETX to recompute parents.
- Same trace hooks for logging.

### 8.3 `EtxTracker`
- EWMA PDR per link: \( \widehat{p}_{tr}(t) = (1-\alpha)\widehat{p}_{tr}(t-1) + \alpha \cdot \mathbb{I}\{\text{ACK}\} \).
- ETX = \(1/\max(\widehat{p}, \epsilon)\); exposes z-score normalized feature for controller.

### 8.4 `Jammer`
- Periodically toggles pathloss offsets or injects `SpectrumPhy` noise to achieve desired \(T_{\text{dyn}}\). Exposes a method to compute empirical autocorrelation to verify target \(T_{\text{dyn}}\).

### 8.5 `MetricsLog`
- Packet registry: for each packet ID, store list of `(node, time)` TX events.
- On delivery: mark “useful” (no action needed). On drop (admission or downstream): traverse registry and increment `W_t` by that packet’s TX count.

---

## 9) CLI & Config

### 9.1 ns-3 harness (`wpan_capture_sim.cc`)
Flags (examples):
```
--seed=123
--nodes=75
--area=1.0
--sink=center
--betaDb=7
--captureDb=6
--txPowerDbm=0
--Tdyn=20ms
--Tinfo=120ms
--rhoSweep="60,120,240,480"  # overrides Tinfo if set
--buffers="10,20,40"
--epsilon=0.05
--sources=15
--V=2.0
--kHopVeto=1
--nuEdits=2
--slotMs=10
--duration=600s
--warmup=60s
--output=out/run_$(date +%s)
```

### 9.2 LPF estimator (`lpf_estimator.py`)
Flags:
```
--topology out/run_X/topology.json
--weights 2000
--maximal-samples 10000
--ilp-solver ortools
--triad-check false
--out out/run_X/lpf/
```

---

## 10) Validation & Sanity Checks

- **PER vs SINR** matches threshold within tolerance.
- **AoI** uniform under cadenced snapshots; verify \(\mathbb{P}\{\Delta ≥ T_{info}/2\}\approx 0.5\).
- **Queue invariants**: no negative queues; max ≤ B.
- **Waste accounting**: on a tiny line test, verify \(W_t ≥ D_t^{down}\).
- **BCP gating**: during “interference ON”, fraction of links with \(\theta_e≥0\) drops; after “OFF”, it rises (“hold breath/exhale”).
- **LPF**: small graphs (N≤20) compare ILP MWIS vs full independent-set enumeration to ensure pipeline correctness.

---

## 11) Outputs & Figures (what scripts will produce)

- **Drops vs \(\rho\)** (per B): Local ~ flat (decays with B), Global ↑ like \(1-e^{-c\rho}\).
- **Waste vs \(\rho\)** similarly.
- **Goodput vs offered load** at fixed \(\rho\): shows region contraction for Global.
- **Waste vs \(B\)** (log-y): Global ~ \(1/B\); Local ~ exp(−ζB).
- **LPF hist & worst-case**: histogram of \(r(w)\), \(\hat{\sigma}\) annotation; selected worst-case weight vector visualization.

---

## 12) Risks & Mitigations

- **SINR non-pairwise nature**: Pairwise conflict graph is an approximation. Mitigate with optional triad-check and note in text; LPF is a **lower bound**.
- **802.15.4 MAC contention**: CSMA/CA randomness may blur schedules. Use slot alignment (periodic attempt windows) and fixed backoff seeds to reduce variability; average over seeds.
- **Queue semantics**: Device-level queues vs app queues; we rely on app queues for finite-buffer logic and use MAC ACKs for fate.
- **ILP scalability**: For many links, MWIS ILP may be slow. Limit to a random subset of links, or use high-quality MW heuristics for larger runs; keep exact ILP for moderate subgraphs or for fewer \(w\).

---

## 13) Phase Plan (no dates)

1. **Bring-up**: ns-3 with lr-wpan + spectrum; single-link PER calibration.
2. **Dynamics**: jammer injection; verify \(T_{\text{dyn}}\).
3. **Controllers**: BackpressureApp and SnapshotGlobalApp minimal paths; end-to-end delivery.
4. **Metrics**: waste & drops accounting; AoI & ETX logging.
5. **Experiment matrix**: B, \(\rho\), ε sweeps; out-of-the-box plots.
6. **LPF pipeline**: export channels; conflict graph; weight sweep; ILP MWIS; report \(\hat{\sigma}\).
7. **Polish**: seed reproducibility, README, container, CI sanity tests.

---

## 14) Acceptance Criteria

- **A1**: Calibration plots show expected PER vs SINR with capture margin x dB.
- **A2**: On line micro-test, waste lower bound behavior matches theory (monotone in \(\rho\), \(1/B\)).
- **A3**: On capture topology, Global curves increase with \(\rho\); Local remains near-flat; buffer sweeps show \(1/B\) vs exp(−ζB) trends.
- **A4**: LPF pipeline returns a stable \(\hat{\sigma}\) (≤1) across seeds, with a clear worst-case \(w^\star\).
- **A5**: All CSVs and JSON exports have schemas documented; scripts reproduce figures from configs.

---

## 15) References (implementation)

- ns-3 modules: `src/lr-wpan`, `src/spectrum`, `src/sixlowpan`, `src/internet`.
- 802.15.4 capture: realized through SINR/error model thresholds; verify in code & with calibration.
- MWIS ILP: `ortools.linear_solver.pywraplp` or `pulp` with CBC/Gurobi if available.
- Graph ops: `networkx`.

---

## 16) Notes on Mapping to Paper Terminology

- \(T_{\text{dyn}}\): autocorrelation e-fold time of ETX/SINR features (verify from logs).
- \(T_{\text{info}}\): snapshot install cadence \(T_{\text{epoch}}\).
- \(\rho=T_{\text{info}}/T_{\text{dyn}}\): set by configs; AoI tail uniform under cadence.
- **Local**: DPP/backpressure gating with ETX penalty (KKT threshold).
- **Global-stale**: ETX-tree (CTP-like) with bounded \(k\)-hop veto budget \(\nu\); add-only improvement, does not change \(\rho\) dependence.
- **LPF \(\hat{\sigma}\)**: reported alongside capture results; explains constants shift under the “structural lift”.
