# PHASES.md — Expanded Phased Plan (Capture/SINR Topology) + LPF Measurement
**Architecture preference (as requested):**
> **Keep 802.15.4 but approximate “routing” in the app**: stay with PacketSocket and add a tiny per-node forwarder (forward to a next-hop MAC based on a preloaded table or gradient). Use *out-of-band* global dissemination of ETX/queue backlogs and global route planning (CTP-like) at snapshot installs—no in-band control required. This preserves LR-WPAN PHY/MAC realism and avoids 6LoWPAN complexity, and keeps the implementation minimal while suiting the theory paper.

This plan is written for direct hand-off. Each phase includes **Goals**, **Deliverables**, **Implementation Checklist**, **Interfaces**, **Tests/Acceptance**, and **Risks/Mitigations**.

## 📊 Project Status Summary (as of 2025-11-02)

| Phase | Title | Status | Notes |
|-------|-------|--------|-------|
| 0 | Repo & Environment | ⚠️ Partial | Repo structure ✅, CMake ✅, ns-3 installed ✅; **Missing**: Dockerfile, bootstrap.sh |
| 1 | PHY Calibration | 🟠 Active | Harness operational ✅, TX power control ✅, CSV outputs ✅; **Issue**: Low packet count (~6 vs 400) |
| 2–6 | Controllers & Routing | ⏳ Pending | Blocked on Phase 1 packet count resolution |
| 7–12 | Experiment Matrix & Analysis | ⏳ Pending | Deferred after Phase 6 completion |

**Quick Summary:**
- **Current Active Work**: Phase 1 (PHY calibration); need to increase OnOff packet bursts for reliable PER statistics.
- **Blockers**: Phase 0 missing Dockerfile/bootstrap (non-critical for dev); Phase 1 packet count too low (critical).
- **Key Achievement**: Full ns-3 stack integrated, transmit power control solved, calibration modes producing CSV/PNG outputs.

---

## Phase 0 — Repo, Tooling, and Reproducible Environment
**Goals:** Bootstrap the repository, build system, and container for reproducibility; pin ns-3 version and Python deps.

**Deliverables:**
- `ns3-bcp-ctp-capture/` repo skeleton and `Dockerfile`.
- CMake or waf build for the `sim/` subtree.
- Pre-commit hooks (clang-format, clang-tidy optional).

**Implementation Checklist:**
- [x] Create repo structure as in `PLAN.md`.
- [ ] Add `Dockerfile` (Ubuntu LTS + ns-3.38, gcc-12, Python 3.10, pip deps `ortools`, `networkx`, `numpy`, `pandas`, `matplotlib`).
- [ ] Add `scripts/bootstrap.sh` to pull/build ns-3 and compile `sim/` with CMake (or waf, pick one and stick).
- [x] Add `configs/` with base YAML files (see later phases). ⚠️ Partial: only `capture_medium.yaml` present; `base.yaml`, `matrix_rho.yaml`, etc. pending.

**Interfaces:** N/A

**Tests/Acceptance:**
- [ ] Container builds and runs `./scripts/run_matrix.sh --dry-run`. ⚠️ Blocked: Dockerfile not yet created.
- [x] `ns-3` example (lr-wpan) compiles (verified in Phase 1 and later work).

**Risks/Mitigations:**
- Build instability → pin exact ns-3 release and compiler; prebuild base image.

**Status (as of 2025-11-02):**
- ✅ **SUBSTANTIAL PROGRESS**: Repo structure complete, CMake build working, ns-3 environment configured, example builds and runs.
- ❌ **BLOCKERS FOR FULL COMPLETION**: Dockerfile and bootstrap.sh not yet created.
- ℹ️ **NOTE**: Project has advanced to Phase 1 (PHY Calibration harness complete and working). Remaining Phase 0 items (Dockerfile, bootstrap.sh) can be completed in parallel with later phases or deferred to a Docker/CI hardening phase (Phase 10–11).

---

## Phase 1 — PHY/MAC Bring-up & Calibration Harness with PacketSocket on 802.15.4
**Goals:** Establish a minimal 802.15.4 network with SINR-based capture; calibrate PHY model (PER vs SINR, capture margin); send packets via `PacketSocket` to neighbors.

**Deliverables:**
- [x] `sim/wpan_capture_sim.cc` with N nodes on `LrWpanNetDevice` + `SpectrumChannel` (with SINR-based capture).
- [x] Calibration harness: `--mode=per-sinr-sweep` and `--mode=capture-test` producing CSV outputs.
- [x] Transmit power control: `LrWpanSpectrumValueHelper::CreateTxPowerSpectralDensity()` to set arbitrary dBm.
- [x] Channel export helper (`channel_export.h/.cc`) for topology + gains export.
- [ ] Application stub `ForwarderApp` (structure in place; routing logic pending Phase 5).
- [x] Trace connections for MAC TX/RX/ACK events; metrics logging to CSV.

**Implementation Checklist:**
- [x] Create N nodes with: `LrWpanNetDevice` + `SpectrumChannel` + `ConstantSpeedPropagationDelayModel`.
- [x] Loss model: `LogDistancePropagationLoss` + Nakagami fading.
- [x] Enable ACKs and set `MaxFrameRetries` (3).
- [x] SINR-based error model with configurable β (capture threshold) and capture margin x.
- [x] Transmit power tuning: -20 dBm achieves SINR range 2–18 dB (spans β=7 dB threshold).
- [x] Calibration modes: PER vs SINR sweep, three-node capture test.
- [ ] ForwarderApp with OnOff traffic; routing logic pending Phase 5.
- [x] Trace hooks wired; metrics CSV output.

**Interfaces (Phase 1):**
```cpp
class ForwarderApp : public ns3::Application {
public:
  void Configure(Mac16Address self, Ptr<PacketSocket> sock, Time tickTime);
  void SetTrafficMode(Mode m); // CALIBRATION, LOCAL, GLOBAL (Phase 5+)
  // Phase 1: OnOff bursts to fixed neighbor for PER measurement
};
```

**Tests/Acceptance:**
- [x] Calibration mode runs: PER vs SINR, capture tests produce CSV.
- [x] SINR control verified: -20 dBm spans capture threshold.
- [ ] PER curve shows sharp knee at β (requires higher packet count; pending OnOff app tuning).
- ⚠️ **KNOWN ISSUE**: Packet transmission count insufficient (~6 vs target 400) under UDP Echo. Workaround: Increase sim duration or switch to raw OnOff bursts.

**Risks/Mitigations:**
- PacketSocket address confusion → use 16-bit short addresses; ensure PAN IDs match.
- SINR nonstationarity → record ETX (Phase 5) for feature signals.

**Status (as of 2025-11-02):**
- ✅ **SUBSTANTIAL PROGRESS**: Calibration harness fully operational; transmit power control working; two calibration modes produce valid CSV/PNG.
- ⚠️ **KNOWN ISSUE**: Low packet transmission count (6 vs 400) limits PER statistics; solutions documented in CLAUDE.md.
- 🔄 **NEXT STEP**: Increase OnOff packet bursts or simulation duration to achieve target 400+ packets for reliable PER estimation.

---

## Phase 2 — Topology Generator & Channel Export
**Goals:** Generate random geometric graphs (RGG) and export large-scale channel state for LPF work.

**Deliverables:**
- `helpers/topology.h/.cc`: place nodes in [0,1]^2, choose sink, set short MAC addresses.
- `helpers/channel_export.h/.cc`: export JSON with node positions, path gains \(g_{ab}\), noise, β, capture margin.

**Implementation Checklist:**
- [ ] Uniform placement with seed; sink at center by default.
- [ ] Compute pairwise pathloss gains (dB) from model; store as matrix.
- [ ] Write `topology.json` in run directory:
```json
{ "nodes":[{"id":0,"x":0.1,"y":0.5,"mac":"0x0001"},...],
  "sink":0, "tx_power_dbm":0, "noise_dbm":-100,
  "beta_db":7, "capture_margin_db":6,
  "gains_db":{"0_1":-80.2,"1_0":-80.0,...} }
```
- [ ] CLI flags to control N, area, seed, sink policy.

**Interfaces:**
```cpp
struct NodeSpec { uint32_t id; double x,y; Mac16Address mac; };
struct Topology { std::vector<NodeSpec> nodes; uint32_t sinkId; };
Topology BuildRGG(uint32_t N, uint64_t seed);
void ExportTopologyJson(const Topology&, const ChannelMatrix&, const std::string& path);
```

**Tests/Acceptance:**
- [ ] Deterministic placement under fixed seed.
- [ ] JSON validated by `lpf/io_utils.py` (to be written).

**Risks/Mitigations:**
- Coordinate-to-gain mismatch → centralize gain computation in one helper used by both simulator and exporter.

---

## Phase 3 — Finite Buffers, Queues & Metrics Skeleton
**Goals:** Implement application-level finite buffers and basic metrics scaffolding, including packet fate tracking for waste.

**Deliverables:**
- `ForwarderApp` with ingress queue (size B) and per-neighbor outgoing queues.
- `MetricsLog` with CSV writers for queues, drops, transmissions, deliveries.

**Implementation Checklist:**
- [ ] **Ingress buffer B:** push exogenous arrivals; on full → `D_t^{adm}++`.
- [ ] **Outgoing selection:** (phase 3: fixed next hop) pop one packet per slot for TX attempt.
- [ ] **Packet registry:** map `PacketUID` → list of TX events (node,time).
- [ ] On **delivery at sink**: mark all TX events for that packet “useful”.
- [ ] On **downstream overflow**: when a node receives a packet and its **ingress buffer is full**, drop packet and log `D_t^{down}++`; trigger waste accounting: all TX for this packet so far contribute to `W_t`.
- [ ] CSV: `drops.csv`, `waste.csv`, `goodput.csv`, `queue.csv` with timestamps.

**Interfaces:**
```cpp
struct TxEvent { uint32_t node; double time; };
class PacketRegistry {
public:
  void OnTx(uint64_t uid, uint32_t node, double time);
  void OnDelivered(uint64_t uid);
  void OnDownstreamDrop(uint64_t uid);
  uint32_t WasteFor(uint64_t uid) const; // count of tx events before terminal drop
};
```

**Tests/Acceptance:**
- [ ] Small chain: force downstream-full event; verify `W_t ≥ D_t^{down}`.
- [ ] No negative queues; queues capped at B.

**Risks/Mitigations:**
- UID reuse across the sim → use `Packet::GetUid()` plus a run-unique prefix if necessary.

---

## Phase 4 — Dynamics & AoI: Jammer + Snapshot Cadence
**Goals:** Realize time-varying conditions (for \(T_{dyn}\)) and the snapshot (AoI) cadence (for \(T_{info}\)).

**Deliverables:**
- `helpers/jammer.h/.cc`: toggles per-link attenuation or injects noise to realize an empirical autocorrelation e-fold at \(T_{dyn}\).
- `helpers/aoi_epoch.h/.cc`: schedules snapshot **install** events every \(T_{info}\); maintains AoI per node.

**Implementation Checklist:**
- [ ] Jammer: ON/OFF pattern (e.g., 20s on / 40s off) or Ornstein–Uhlenbeck attenuation; expose `MeasureAutocorr()` to log observed \(T_{dyn}\).
- [ ] AoI cadence: install snapshots at `t = m * T_info` regardless of dissemination completion (AoI uniform on [0, T_info]).
- [ ] Logging: AoI time series; verify uniformity empirically.

**Interfaces:**
```cpp
class Jammer {
 public:
  void Configure(Time on, Time off, double attnDb);
  void Start();
  double EstimateTdyn(const std::vector<double>& featureTrace) const;
};

class SnapshotCadence {
 public:
  void SetEpoch(Time Tinfo);
  void AddCallback(std::function<void(uint64_t epochId)> onInstall);
};
```

**Tests/Acceptance:**
- [ ] Feature autocorrelation decays ~exp(−t/T_dyn) with target T_dyn (tolerance window).
- [ ] AoI histogram ~ uniform; P{Δ ≥ T_info/2} ≈ 0.5.

**Risks/Mitigations:**
- SINR nonstationarity → record ETX (Phase 5) and compute T_dyn from ETX trace instead of attenuation only.

---

## Phase 5 — ETX Tracker & App-Layer Forwarder Interfaces
**Goals:** Estimate ETX per directed link and finish the app-level forwarding API to serve both controllers (Local and Global).

**Deliverables:**
- `controllers/etx_tracker.h/.cc` with EWMA PDR and ETX per link.
- `ForwarderApp` upgraded with two modes: `LOCAL` (DPP/backpressure gating) and `GLOBAL` (tree forwarding).

**Implementation Checklist:**
- [ ] Hook `MacTxOk`/`MacTxDrop` to update PDR for the `(tx→rx)` directed link; ETX = `1 / max(PDR, ε)`.
- [ ] Provide normalized feature (z-score of ETX) for controller score computations.
- [ ] Extend `ForwarderApp` to support:
  - `SetMode(Local|Global)`.
  - `SetParent(Mac16Address)` and `SetGradient(std::vector<NeighborScore>)` (for Local).
  - `PopulateNextHopTable(map<Mac16Address,double>)` for per-neighbor scoring.
- [ ] One TX attempt per slot: pick **best neighbor** under the active controller’s rule, or idle.

**Interfaces:**
```cpp
class EtxTracker {
 public:
  void OnTxResult(Mac16Address tx, Mac16Address rx, bool ackOk);
  double GetEtx(Mac16Address tx, Mac16Address rx) const;
  double GetZScore(Mac16Address tx, Mac16Address rx) const;
};
enum Mode { LOCAL, GLOBAL };
class ForwarderApp : public Application {
  void SetMode(Mode m);
  void SetParent(Mac16Address p);
  void SetDppParams(double V, double lambda);
  void SetEtxTracker(Ptr<EtxTracker>);
  void SetBufferLimit(uint32_t B);
  void Tick(); // per-slot decision
};
```

**Tests/Acceptance:**
- [ ] ETX converges near `1/PDR` on static link.
- [ ] Local/Global modes compile and send to expected next-hop under simple scenarios.

**Risks/Mitigations:**
- ETX bias on small samples → cap EWMA α and set minimum sample count before trusting ETX for decisions.

---

## Phase 6 — Controller Logic: Local (DPP^λ) & Snapshot-Global (CTP-like) w/ Veto
**Goals:** Implement Local backpressure gating and Global snapshot-based routing with \(k\)-hop veto and primitive edit budget \(ν\).

**Deliverables:**
- `controllers/backpressure.h/.cc` and `controllers/snapshot_global.h/.cc`.
- Simple **tree builder**: min-ETX spanning tree to sink at snapshot times.

**Implementation Checklist:**
- [ ] **Local DPP^λ** decision per slot for node `i`:
  - For each neighbor `j`: compute \( \theta_{i\to j}(t) = (Q_i - Q_j) - V \cdot \text{ETX}_{i\to j} \).
  - Select `j* = argmax θ_{i→j}(t)`; if \( \theta_{j*} ≥ 0\) attempt one PacketSocket send to `j*`; else idle.
  - (Optional) If multiple neighbors allowed per slot in later variants, subtract \( \frac{V\lambda}{2}\|x(t)\|_2^2 \) in score when choosing a second link; default: one per slot.
- [ ] **Snapshot-Global** at epoch install:
  - Freeze ETX matrix \(\widehat{\text{ETX}}\).
  - Build min-cost tree (Dijkstra on ETX) from all nodes to sink.
  - Set each node’s `parent` accordingly.
- [ ] **\(k\)-hop veto** (budget \(ν\) primitive edits/slot):
  - On each slot, check `θ_{i→parent(i)}`; if negative and exists neighbor `j` s.t. `θ_{i→j} ≥ τ`, perform a **redirect** `parent(i) → j` for this slot (counts as 2 edits). Limit global count to \(ν\).
  - Count edits; log `C_veto(k,B)` per slot.
- [ ] **Out-of-band snapshot install**: `SnapshotCadence` calls a single function that reads all queues + ETX from nodes, computes tree, and writes parents back—no in-band signaling.

**Interfaces:**
```cpp
class SnapshotGlobal {
 public:
  void InstallSnapshot(const Topology&, const EtxTracker&, const std::vector<uint32_t>& Q);
  Mac16Address GetParent(uint32_t nodeId) const;
};

struct VetoConfig { uint32_t k; uint32_t nu; double tau; };
class VetoEngine {
 public:
  void Step(std::vector<NodeState>& nodes, const VetoConfig& cfg, uint32_t& editsUsed);
};
```

**Tests/Acceptance:**
- [ ] Local controller shows “hold-breath/exhale”: during jammer ON, fraction of links with `θ≥0` drops; OFF it rises.
- [ ] Global follows tree; with veto enabled, a limited number of redirects occur per slot and are logged.

**Risks/Mitigations:**
- Starvation under Local if `V` too large → sweep `V` in micro-tests; cap ETX to reasonable range.

---

## Phase 7 — Traffic, ε-Slack Calibration, and Experiment Driver
**Goals:** Provide traffic sources, calibrate offered load for target ε-slack under Local, and run the full experiment matrix (B, ρ, β, x).

**Deliverables:**
- `apps/source_app.h/.cc` and `apps/sink_app.h/.cc`.
- `scripts/run_matrix.sh` and `scripts/postprocess.py`.

**Implementation Checklist:**
- [ ] **Traffic**: K sources (default floor(0.2N)); Bernoulli or Poisson arrivals to ingress buffer.
- [ ] **ε-calibration**: run Local-only short warmup; binary search per-source λ to achieve desired ε slack (service surplus) measured by drift surrogate.
- [ ] **Matrix driver**: sweep `B ∈ {10,20,40}`, `ρ ∈ {0.5,1,2,4,6}`, `β`, `capture margin x`, seeds (≥10).
- [ ] **Outputs**: CSV + JSON per run; merge in `postprocess.py` to compute means/CIs.

**Interfaces:**
```bash
./wpan_capture_sim --nodes=75 --B=20 --rho=4 --betaDb=7 --captureDb=6 \
  --epsilon=0.05 --sources=15 --duration=600s --warmup=60s --seed=123 \
  --output=out/run_123/
```

**Tests/Acceptance:**
- [ ] Calibration converges to target ε within tolerance.
- [ ] End-to-end runs produce non-empty CSVs; CI bands computed.

**Risks/Mitigations:**
- Calibration unstable under jammer → increase warmup; smooth measurements over windows.

---

## Phase 8 — LPF Measurement Pipeline (Python)
**Goals:** Compute a **capture-aware conflict graph** and estimate \(\hat{\sigma}\) by weight-sweep comparing GMS vs MWIS.

**Deliverables:**
- `lpf/build_conflict_graph.py`, `lpf/lpf_estimator.py`, `lpf/schedule_utils.py`, `lpf/io_utils.py`.
- `scripts/compute_lpf.sh`.

**Implementation Checklist:**
- [ ] Read `topology.json`; build directed link set (communication reach based on sensitivity).
- [ ] Conflict edge between links \(\ell_1,\ell_2\) if **either** receiver’s SINR < β when both transmit (use exported gains + powers + N0).
- [ ] Enumerate **maximal independent sets** (MIS) via randomized greedy (M≈5k–20k).
- [ ] For T≈2000 random nonnegative weights `w`:
  - **GMS**: greedy pick by descending `w`; compute `f_G = w·s_G`.
  - **MW oracle**: solve MWIS via ILP (`ortools`/`pulp`); compute `f_M`.
  - Record ratio `r = f_G/f_M`.
- [ ] Report \(\hat{\sigma} = \min r\) and histogram; save to `out/run_X/lpf/`.

**Interfaces (CLI):**
```bash
python -m lpf.build_conflict_graph --topology out/run_X/topology.json --out out/run_X/lpf/graph.gexf
python -m lpf.lpf_estimator --graph out/run_X/lpf/graph.gexf --weights 2000 --mis 10000 --solver ortools --out out/run_X/lpf/
```

**Tests/Acceptance:**
- [ ] Small graphs (N≤20): validate against exact independent-set enumeration.
- [ ] Ratios ∈ (0,1]; \(\hat{\sigma}\) stable across seeds within error bars.

**Risks/Mitigations:**
- ILP scalability → cap link count, sample subgraphs, or use heuristic MW for large graphs; state that \(\hat{\sigma}\) is a lower bound.

---

## Phase 9 — Figures, Analysis & Paper Hooks
**Goals:** Generate all figures and tables; produce text-ready numbers (e.g., \(\hat{\sigma}\)).

**Deliverables:**
- `scripts/plot_recipes.py`: 
  - Drops/Waste vs ρ (per B),
  - Goodput vs offered load (region contraction),
  - Waste vs B (log-y),
  - LPF histogram (with \(\hat{\sigma}\)).
- `scripts/postprocess.py`: summary CSVs + confidence intervals.

**Implementation Checklist:**
- [ ] Implement plotting as standalone Python (matplotlib); pull from merged CSVs.
- [ ] Inject \(\hat{\sigma}\) annotation in capture plots.
- [ ] Export `figures/` and `tables/summary.csv`.

**Tests/Acceptance:**
- [ ] Curves monotone in ρ (Global grows, Local ~flat); B spacing correct.
- [ ] LPF histogram sensible; worst-case weight visual logged.

**Risks/Mitigations:**
- Noisy curves → increase seeds or sim time; smooth with CIs, not moving averages.

---

## Phase 10 — Reproducibility & Packaging
**Goals:** Make it turnkey for reviewers and artifact evaluators.

**Deliverables:**
- `README.md` with step-by-step run.
- Version-pinned `Dockerfile` and `requirements.txt`.
- `scripts/run_all.sh` to reproduce headline figures end-to-end.

**Implementation Checklist:**
- [ ] Document config schemas and default values.
- [ ] Add smoke tests in CI (e.g., GitHub Actions with a tiny run).
- [ ] Ensure seeds are logged and used consistently.

**Tests/Acceptance:**
- [ ] Fresh clone + container build + `run_all.sh` produces all figures without manual edits.

**Risks/Mitigations:**
- Container bloat → pre-download ns-3 or use multi-stage build.

---

## Phase 11 — Validation & Micro-benchmarks (Optional but Recommended)
**Goals:** Strengthen confidence in the mechanics and measurements.

**Deliverables:**
- Micro-sim scripts:
  - **PER vs SINR** (one link, with/without interferer) to verify capture threshold.
  - **AoI uniformity** check under cadence.
  - **Waste ≥ downstream drops** invariant on a line.
- Unit tests for `EtxTracker`, `PacketRegistry`, `VetoEngine` (where feasible).

**Implementation Checklist:**
- [ ] Add `sim/tests/` with small scenarios and asserts.
- [ ] Add Python checkers to parse CSVs and verify conditions.

**Tests/Acceptance:**
- [ ] All micro-tests pass in CI.

**Risks/Mitigations:**
- Stochastic flakiness → widen tolerances; seed control.

---

## Phase 12 — Extensions (If Time Permits)
**Goals:** Stress the “structural constant” idea and show robustness.

**Deliverables:**
- Variants for capture β and power control; recompute \(\hat{\sigma}\) and overlay curves.
- Optional **triad-check** pass in conflict graph construction.

**Implementation Checklist:**
- [ ] Add configs for β ∈ {5,7,9} dB and capture margin x ∈ {3,6}.
- [ ] Run LPF and main sims for each; compare constants (not slopes).

**Tests/Acceptance:**
- [ ] \(\hat{\sigma}\) moves with β/x as expected; qualitative ρ and 1/B dependencies preserved.

**Risks/Mitigations:**
- Explosion of run count → prioritize 1–2 representative settings for the paper; move others to appendix.

---

## Controller Pseudocode (App-layer Routing)

**Local (DPP^λ, one TX/slot):**
```text
for node i each slot t:
  bestTheta = -inf; bestJ = None
  for neighbor j in Neighbors(i):
    theta = (Q[i] - Q[j]) - V * ETX[i->j]
    if theta > bestTheta: bestTheta = theta; bestJ = j
  if bestTheta >= 0 and Q[i] > 0:
    send one packet via PacketSocket to bestJ
  else:
    idle
```

**Snapshot-Global (CTP-like) + k-hop veto:**
```text
At install times T_m:
  freeze ETX_hat
  compute parent(i) for all i via Dijkstra on ETX_hat
Each slot t:
  editsUsed = 0
  for nodes i in some order:
    if Q[i] == 0: continue
    j = parent(i)
    theta_parent = (Q[i] - Q[j]) - V * ETX[i->j]
    if theta_parent < 0 and editsUsed + 2 <= nu:
      find neighbor k with theta_k = (Q[i] - Q[k]) - V * ETX[i->k] >= tau
      if such k exists:
        redirect i->k for this slot (2 edits)
        editsUsed += 2; send to k
      else:
        send to j  // follow plan
    else:
      send to j  // follow plan
```

---

## Notes on Architectural Choices
- **PacketSocket + app routing** keeps LR-WPAN PHY/MAC in play (ACKs, CSMA/CA, retries) and avoids 6LoWPAN overhead. Routing (Local vs Global) is **purely inside the application**, set by *out-of-band* snapshot installs.
- **Out-of-band global coordination** mirrors the paper’s abstraction: AoI is a cadence; the install step reads current ETX/queues centrally and writes parent tables—no L2 control needed.
- **One TX per slot per node** matches radio reality; concurrency arises across nodes, not within a node, and capture-aware SINR decides success.

---

## Hand-off Summary
- After **Phase 6**, you can already run small experiments to see the core phenomena (ρ sweep, B sweep) with basic plots.
- After **Phase 8**, you can report \(\hat{\sigma}\) and attach it to capture-topology results.
- **Phases 9–10** deliver the end-to-end paper artifacts with reproducibility.

