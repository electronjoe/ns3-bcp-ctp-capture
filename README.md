# NS3-BCP-CTP-CAPTURE

Scenario B: Capture-aware 802.15.4 network simulation with Local (backpressure) vs Snapshot-Global (tree + veto) controllers, finite-buffer analysis, and LPF measurement.

See **CLAUDE.md** for detailed project guidance and **plan/** directory for phased implementation plan.

---

## Environment Setup

### Prerequisites
- C++ compiler (gcc 10+ or clang 12+)
- CMake 3.20+
- Python 3.10+ with matplotlib
- ns-3 (3.38+) installed to `$HOME/opt/ns3`

### Install ns-3

```bash
git clone https://gitlab.com/nsnam/ns-3-dev.git
cd ns-3-dev
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/opt/ns3
cmake --build build -j$(nproc)
cmake --build build --target install

# Set environment variable for CMake to find ns-3
export CMAKE_PREFIX_PATH="$HOME/opt/ns3:$CMAKE_PREFIX_PATH"
```

**Note:** Add the `export CMAKE_PREFIX_PATH` line to your `~/.bashrc` or `~/.zshrc` to make it permanent.

---

## Example Build and Run (Verification)

The `example/` directory contains a working 4-node line topology to verify your ns-3 installation:

```bash
cd example
mkdir -p build && cd build
cmake -DCMAKE_PREFIX_PATH="$HOME/opt/ns3" ..
make -j$(nproc)
./line-lr-wpan
```

Running the binary generates PCAP files (one per node):
```bash
ls *.pcap
# line-lr-wpan-0-0.pcap  line-lr-wpan-1-0.pcap  line-lr-wpan-2-0.pcap  line-lr-wpan-3-0.pcap
```

To clean:
```bash
make clean
```

---

## Simulation Harness (Phase 1+)

The main simulation harness is in `sim/` directory:

### Build

```bash
cd sim
mkdir -p build && cd build
cmake -DCMAKE_PREFIX_PATH="$HOME/opt/ns3" ..
make -j$(nproc)
```

**Note:** The plan documents reference Ninja as the build generator, but standard Unix Makefiles work fine. If you have Ninja installed, you can use `-G Ninja` with cmake.

### Run Phase 1 Calibration

```bash
# From sim/build directory

# PER vs SINR sweep
./wpan-capture-sim \
  --mode=per-sinr-sweep \
  --betaDb=7 \
  --txPowerDbm=0 \
  --noiseFigureDb=10 \
  --bandwidthHz=2000000 \
  --out=../../out/calibration

# Capture margin test
./wpan-capture-sim \
  --mode=capture-test \
  --captureDb=6 \
  --betaDb=7 \
  --txPowerDbm=0 \
  --noiseFigureDb=10 \
  --bandwidthHz=2000000 \
  --out=../../out/calibration

# Generate plots
python3 ../../scripts/plot_recipes.py per ../../out/calibration/per_vs_sinr.csv ../../out/calibration/per_vs_sinr.png
python3 ../../scripts/plot_recipes.py cap ../../out/calibration/capture_toggle.csv ../../out/calibration/capture_toggle.png
```

---

## Known Issues

### Phase 1 Calibration (Current)

1. **Transmit Power Control:** ns-3 `LrWpanPhy` does not expose a simple `SetTransmitPower()` or `TxPower` attribute. The current implementation uses default transmit power, resulting in very high SINR values (22-38 dB). This prevents observation of PER variation near the beta threshold.

2. **Low Packet Counts:** Only ~6 packets are transmitted per test instead of the configured 400, limiting statistical significance.

3. **No PER Variation:** All calibration points show 0% PER because SINR is too high for the tested distance range (6-20m).

**Workaround Options:**
- Use much larger distances (100-500m) to lower SINR
- Adjust propagation model parameters (referenceLossDb, exponent)
- Research correct ns-3 API for LrWpanPhy power control

See **plan/PHASE_1_RESULTS.md** for detailed analysis and recommended next steps.

---

## Project Structure

```
ns3-bcp-ctp-capture/
├─ example/              # Working 4-node example (verification)
├─ sim/                  # Main simulation harness
│  ├─ wpan_capture_sim.cc
│  ├─ helpers/          # Channel export, metrics, etc.
│  └─ CMakeLists.txt
├─ configs/              # YAML configuration files
├─ scripts/              # Run scripts and plotting utilities
├─ plan/                 # Implementation plan and phase documentation
│  ├─ PLAN.md           # High-level design
│  ├─ PHASES.md         # Detailed phase breakdown
│  ├─ PHASE_0.md        # Bootstrap instructions
│  ├─ PHASE_1.md        # PHY calibration (current)
│  └─ PHASE_1_RESULTS.md # Current results and issues
└─ out/                  # Simulation results and plots
```

---

## Documentation

- **CLAUDE.md** - Guidance for Claude Code when working with this repo
- **plan/PLAN.md** - Complete simulation design and architecture
- **plan/PHASES.md** - Phase-by-phase implementation guide
- **plan/PHASE_X.md** - Detailed instructions for each phase

---

## Status

- ✅ **Phase 0:** Environment setup and example verification
- 🟡 **Phase 1:** PHY calibration (infrastructure complete, calibration issues to resolve)
- ⏳ **Phase 2+:** Pending