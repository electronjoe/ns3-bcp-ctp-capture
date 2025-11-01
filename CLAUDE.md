# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ns-3 network simulation project implementing **Scenario B**: a capture-aware 802.15.4 (IEEE 802.15.4 lr-wpan) wireless network simulator with two forwarding control strategies and a Local Pooling Factor (LPF) measurement pipeline.

**Key objectives:**
- Simulate capture-aware communication using SINR-based PHY models
- Compare two routing/MAC strategies: Local (backpressure) vs Snapshot-Global (tree with vetoes)
- Measure finite-buffer effects (admission drops, downstream drops, wasted transmissions)
- Compute the **LPF** (σ̂) to quantify the "structural lift" gained by scheduling on capture-aware graphs

## Repository Structure

```
ns3-bcp-ctp-capture/
├─ example/                 # Working standalone ns-3 example
│  ├─ CMakeLists.txt       # CMake build config (link to installed ns-3)
│  └─ src/line-lr-wpan.cc  # 4-node line topology with UDP echo
├─ plan/                    # Phase-driven implementation plan
│  ├─ PLAN.md              # High-level design (topology, channels, controllers, LPF)
│  ├─ PHASES.md            # Detailed phases 0–12 (objectives, tasks, DoD)
│  └─ PHASE_0.md           # Bootstrap: ns-3 install and example verification
├─ capture_to_llm.py        # Helper: concatenates repo to LLM.md (respects .gitignore)
├─ LLM.md                   # Generated snapshot of all repo files
└─ README.md               # Basic setup instructions

**Current structure (as of Phase 1):**
├─ sim/                     # C++ ns-3 harness
│  ├─ wpan_capture_sim.cc  # Main simulation entry point (Phase 1: calibration modes)
│  ├─ helpers/            # Channel export utilities
│  │  ├─ channel_export.h
│  │  └─ channel_export.cc
│  ├─ build/              # CMake build directory (gitignored)
│  └─ CMakeLists.txt      # Build config
├─ configs/                # YAML config files
│  └─ capture_medium.yaml # PHY/MAC parameters
├─ scripts/                # Run harness, post-processing, plotting
│  └─ plot_recipes.py     # Calibration plotting
└─ out/                    # Results (CSV, JSON, plots)
   └─ calibration/        # Phase 1 outputs

**Future additions (Phases 2+):**
├─ sim/controllers/        # Local and Global routing logic
├─ sim/apps/              # Source/sink applications
├─ lpf/                   # Python LPF estimation pipeline
```

## Build & Run Commands

### Phase 0: Example Project (Verification)

The example demonstrates a standalone 4-node line topology with ns-3. To build and run:

```bash
cd example
mkdir -p build && cd build
cmake -DCMAKE_PREFIX_PATH="$HOME/opt/ns3" ..
make -j$(nproc)
./line-lr-wpan
```

Produces: `line-lr-wpan-*.pcap` files (one per node) in the current directory.

**Environment setup** (one-time):
- ns-3 must be installed to `$HOME/opt/ns3` with CMake support
- Set environment in shell: `export CMAKE_PREFIX_PATH="$HOME/opt/ns3:$CMAKE_PREFIX_PATH"`
- See PHASE_0.md for detailed ns-3 installation steps

**Note:** Original plan references Ninja (`-G Ninja`), but standard Unix Makefiles work fine if Ninja is not installed.

### Phase 1: Calibration Harness (Current)

Build and run the PHY calibration modes:

```bash
# Build
cd sim
mkdir -p build && cd build
cmake -DCMAKE_PREFIX_PATH="$HOME/opt/ns3" ..
make -j$(nproc)

# PER vs SINR sweep (use -20 dBm to achieve SINR near beta threshold)
./wpan-capture-sim --mode=per-sinr-sweep --betaDb=7 --txPowerDbm=-20 \
  --noiseFigureDb=10 --bandwidthHz=2000000 --out=../../out/calibration

# Capture margin test
./wpan-capture-sim --mode=capture-test --captureDb=6 --betaDb=7 \
  --txPowerDbm=-20 --noiseFigureDb=10 --bandwidthHz=2000000 --out=../../out/calibration

# Generate plots
python3 ../../scripts/plot_recipes.py per ../../out/calibration/per_vs_sinr.csv ../../out/calibration/per_vs_sinr.png
python3 ../../scripts/plot_recipes.py cap ../../out/calibration/capture_toggle.csv ../../out/calibration/capture_toggle.png
```

### Future: Experiment Matrix (Phases 2+)

Once controllers and full harness are implemented:

```bash
# Experiment matrix (Phase 7)
scripts/run_matrix.sh --matrix configs/matrix_rho.yaml --out out/matrix

# LPF estimation (Phase 8)
python3 lpf/lpf_estimator.py --Gc out/run_123/Gc.graphml --weights 2000 --out out/run_123/lpf
```

## Architecture & Design Notes

### Simulation Scope (Scenario B)

1. **Topology**: N ∈ {50, 75, 100} nodes uniformly random in [0,1]² (RGG); fixed sink.
2. **PHY model**: 802.15.4 with SINR-based capture threshold β and capture margin x dB.
3. **Channels**: SpectrumChannel with LogDistancePropagationLoss + Nakagami small-scale fading.
4. **Dynamics**: Time-varying interference via jammer (ON/OFF periods) with correlation time T_dyn.
5. **Snapshots**: Cadenced ETX-tree install at intervals T_info; cadence ratio ρ = T_info/T_dyn.

### Controllers

**Local (DPP^λ / Backpressure):**
- Per-slot computation of pressure θ_e(t) = (Q_i - Q_j) - V·ETX_e(t)
- Gate transmission on link e if θ_e ≥ 0
- Fresh per-slot features; no stale information

**Snapshot-Global (ETX-tree + k-hop veto):**
- Freeze ETX at snapshot install times; run Dijkstra to compute parent map
- Forward only to parent between snapshots
- k-hop veto budget (ν edits/slot) allows local improvements without changing topology structure

### Key Metrics

Per time slot or aggregated:
- **D_adm**: admission drops (queue full at arrival)
- **D_down**: downstream drops (next-hop queue full)
- **W_t**: wasted transmissions (sum of upstream TX counts on dropped packets)
- **Goodput**: sink delivery rate
- **AoI**: age of snapshots (should be uniform on [0, T_info] under cadence)
- **ETX**: per-link delivery ratio estimation (EWMA-based)

### LPF Measurement Pipeline

1. **Export topology** from ns-3: JSON with node positions, pathloss gains (dB), Tx powers, noise, β, x.
2. **Build conflict graph G_c**: pairwise-feasibility checks; edge iff link pair cannot coexist under SINR.
3. **Enumerate schedules**: randomized greedy maximal independent sets (M ≈ 5k–20k samples).
4. **Weight sweep**: for T ≈ 2000 random weights w, compare greedy schedule vs MWIS (ILP), compute ratio r(w).
5. **LPF estimate**: σ̂ = min_w r(w); report histogram and worst-case weight.

## Configuration & Parameterization

Key CLI flags for the main harness (wpan_capture_sim):
- `--seed`: random seed (default varies)
- `--nodes`: network size N
- `--betaDb`: SINR capture threshold (dB)
- `--captureDb`: capture margin x (dB)
- `--Tdyn`: dynamics correlation time (ms)
- `--Tinfo`: snapshot cadence (ms)
- `--buffers`: finite queue size B (packets)
- `--epsilon`: ε-slack tuning
- `--duration`, `--warmup`: simulation length and warmup window
- `--output`: result directory

YAML configs (configs/*.yaml):
- `base.yaml`: common defaults
- `capture_medium.yaml`: PHY/MAC parameters locked in after Phase 1 calibration
- `matrix_rho.yaml`, `buffers.yaml`, `traffic.yaml`: experiment grids

## Testing & Validation

**Sanity checks (from PLAN.md, section 10):**
1. **PER vs SINR**: Verify calibration curve has sharp knee at β.
2. **Capture margin**: Three-node test confirms interference capture x dB margin.
3. **AoI uniformity**: Under cadence, P{Δ ≥ T_info/2} ≈ 0.5.
4. **Queue invariants**: No negative queues; max ≤ B.
5. **Waste inequality**: On line test, W_t ≥ D_t^{down}.
6. **BCP gating**: Jammer ON → fewer links pass θ_e ≥ 0; OFF → recovery.
7. **LPF stability**: σ̂ consistent across seeds and topology repeats.

## Key Files to Understand First

**When starting future phases:**
1. **plan/PLAN.md**: High-level design (what and why)
2. **plan/PHASES.md**: Phase breakdown (how and tasks)
3. **plan/PHASE_0.md**: Concrete bootstrap steps
4. **example/**: Canonical working example (builds, runs, produces PCAP)
5. **README.md**: Quick start (current state)

**When implementing Phase X:**
- Check PHASES.md for that phase's objectives, inputs, artifacts, validation, and DoD
- Reference PLAN.md (section 8) for class sketches (BackpressureApp, SnapshotGlobalApp, EtxTracker, etc.)
- Use PLAN.md (section 9) for CLI flags and config structure

## Common Development Tasks

### Adding a new controller variant

1. Create `sim/controllers/variant_name.h/.cc` following BackpressureApp or SnapshotGlobalApp as template.
2. Implement the core decision logic and expose hooks for metrics logging.
3. Add CLI flag in wpan_capture_sim.cc to select this variant.
4. Update YAML config schema to document new parameters.
5. Add validation checks in PHASES.md or testing section.

### Extending metrics logging

1. Modify `sim/helpers/metrics_log.h/.cc` to track new fields.
2. Update CSV schema documentation in PLAN.md (Appendix B).
3. Ensure per-slot or per-packet accounting is consistent with waste/drop logic.
4. Add post-processing in scripts/postprocess.py if aggregation is needed.

### Adding LPF variations (e.g., triad check)

1. Enhance `lpf/build_conflict_graph.py` to optionally prune false independence.
2. Update `lpf/lpf_estimator.py` to accept new graph variant flags.
3. Document trade-offs in PLAN.md risk register.
4. Validate against small graphs (N ≤ 20) by comparing to brute-force enumeration.

## Reproducibility & Seeding

- Default seed list: will be fixed in Phase 7 (see PHASES.md, Phase 11).
- Use `--seed=N` flag to pin runs for debugging.
- Run matrix with fixed seed list to ensure reproducibility across machines.
- CI (Phase 11) will containerize the full stack for bit-for-bit reproducibility.

## References & Related Work

- **ns-3 modules**: lr-wpan, spectrum, sixlowpan, internet, applications, flow-monitor.
- **802.15.4 capture**: Realized via SINR error model; calibrated in Phase 1.
- **MWIS solver**: ortools (linear_solver.pywraplp) or pulp + CBC/Gurobi.
- **Graphs**: networkx for conflict graph construction and analysis.

## Phase Roadmap (At a Glance)

| Phase | Title | Key Deliverable |
|-------|-------|-----------------|
| 0 | Bootstrap & Environment | Example builds/runs; ns-3 installed |
| 1 | PHY Calibration | `capture_medium.yaml` with β, x finalized; PER vs SINR plot |
| 2 | Dynamics Model | Jammer component; verify T_dyn from autocorrelation |
| 3 | Finite Buffers | Queue logic; waste/drop accounting; unit test |
| 4 | ETX Tracker | Per-link EWMA PDR; normalized features |
| 5 | Local Controller | Backpressure gating; stable behavior under ε-slack |
| 6 | Global Controller | ETX-tree + k-hop veto; AoI tracking |
| 7 | Experiment Harness | One binary; CLI matrix runner; reproducibility |
| 8 | Topology/LPF Export | Conflict graph; weight sweep; σ̂ estimation |
| 9 | Figures & Post-Processing | Main paper plots with CIs |
| 10 | Cross-Checks & QA | Validation checklist sign-off |
| 11 | Docker & CI | Reproducible container; CI green |
| 12 | Variants & Ablations | Robustness studies (optional) |

See **plan/PHASES.md** for detailed phase definitions.

## Current Status & Known Issues

### Phase 1: PHY Calibration (Partial Completion)

**Completed:**
- ✅ Full infrastructure: CMake build, calibration harness, plotting utilities
- ✅ Two calibration modes functional (per-sinr-sweep, capture-test)
- ✅ CSV and PNG outputs generated
- ✅ IPv6/UDP Echo working over 6LoWPAN

**Resolved Issues:**

1. ✅ **ns-3 LrWpanPhy Transmit Power Control** (FIXED)
   - Solution: Using `LrWpanSpectrumValueHelper::CreateTxPowerSpectralDensity()`
   - Implementation: `SetTxDbmAndNoise()` helper function in `sim/wpan_capture_sim.cc`
   - Result: Can now set arbitrary transmit power in dBm
   - Verification: -20 dBm achieves SINR range 2-18 dB (spans beta threshold of 7 dB)
   - See: `plan/PHASE_1_POWER_FIX_RESULTS.md` for details

2. ✅ **SINR Control** (FIXED)
   - Achieved by setting transmit power to -20 dBm
   - Distance range 6-20m now produces SINR: 2-18 dB
   - Successfully spans beta threshold (7 dB) for calibration
   - Per-node power setting working for capture tests

**Remaining Issues:**

1. ⚠️ **Low Packet Transmission Count**
   - Issue: Only ~6 packets transmitted per test instead of configured 400
   - Likely causes: IPv6 neighbor discovery delays, UDP Echo response waiting, MAC initialization
   - Impact: Insufficient samples for statistical PER measurement (need 400+ for reliable statistics)
   - Affected: Both per-sinr-sweep and capture-test modes
   - **Next step:** Increase simulation duration (to 60s+) or switch to OnOff application
   - **Blocking:** Phase 1 DoD completion (need to observe PER variation)

**See plan/PHASE_1_POWER_FIX_RESULTS.md for latest results and next steps.**

### Build System Notes

- **CMake Generator:** Plan documents reference Ninja, but Unix Makefiles work fine
  - Use `-G Ninja` if Ninja is installed
  - Otherwise, omit `-G` flag and cmake will use Unix Makefiles by default

- **IPv6 Routing:** Must use actual assigned IPv6 addresses from `Ipv6InterfaceContainer::GetAddress(index, 1)` instead of hardcoded addresses like `2001:db8:calib::2`

- **ns-3 API Differences:**
  - Use `SetAttribute()` not `GetAttribute()` to set PHY parameters
  - `SetDefaultRouteInAllNodes()` must be called after `SetForwarding()` for proper IPv6 routing

## Troubleshooting

### Build Errors

**"CMake was unable to find a build program corresponding to Ninja"**
- Solution: Omit `-G Ninja` flag, use default Unix Makefiles generator
- Or install Ninja: `sudo apt-get install ninja-build`

**"CMAKE_CXX_COMPILER not set"**
- Solution: Install build tools: `sudo apt-get install build-essential cmake g++`

**"cannot bind non-const lvalue reference"**
- Cause: Using `GetAttribute()` instead of `SetAttribute()`
- Solution: Change to `SetAttribute()`

### Runtime Errors

**"Attribute name=TxPower does not exist"** ✅ SOLVED
- Cause: `LrWpanPhy` doesn't have a `TxPower` attribute
- Solution: Use `LrWpanSpectrumValueHelper::CreateTxPowerSpectralDensity()`
- Example:
  ```cpp
  LrWpanSpectrumValueHelper helper;
  double txW = DbmToW(txDbm);
  Ptr<SpectrumValue> txPsd = helper.CreateTxPowerSpectralDensity(txW, channelNumber);
  phy->SetTxPowerSpectralDensity(txPsd);
  ```
- See `SetTxDbmAndNoise()` in `sim/wpan_capture_sim.cc` for full implementation

**"Can not find an outgoing interface for a packet"**
- Cause: IPv6 routing not properly configured
- Solution: Ensure `SetForwarding(true)` and `SetDefaultRouteInAllNodes()` called on all interfaces
- Solution: Use actual assigned IPv6 addresses, not hardcoded ones

**Only 6 packets sent instead of 400**
- Cause: Application stop time too early, or neighbor discovery consuming time
- Workaround: Increase simulation duration
- Investigation needed: Add logging to understand MAC/app behavior

## Notes for Future Developers

- **Terminology mapping** (PLAN.md, section 16):
  - T_dyn = autocorrelation e-fold time of ETX/SINR
  - T_info = snapshot cadence
  - ρ = T_info/T_dyn (the key timescale ratio)
  - Local = DPP/backpressure with fresh features
  - Global = ETX-tree with bounded veto budget
  - LPF σ̂ = min_w (greedy / optimal) under weight sweep

- **Risk mitigation** (PLAN.md, section 12):
  - SINR is non-pairwise for 3+ concurrent links; pairwise conflict graph is conservative.
  - MWIS ILP may be slow for large graphs; use heuristics or subgraph isolation.
  - MAC contention (CSMA/CA) can blur intended schedules; use slot alignment and fixed backoff seeds.

- **When to consult documentation**:
  - Phase objectives & DoD: PHASES.md
  - Class/API sketches: PLAN.md, section 8
  - CLI & config: PLAN.md, section 9
  - Expected outputs & figures: PLAN.md, section 11
  - QA checks: PHASES.md, Phase 10

---

**Last updated:** 2025-11-01
**Repository:** ns3-bcp-ctp-capture (ns-3 + LPF measurement)
