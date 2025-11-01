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
# Note: Use -20 dBm power to achieve SINR range near beta threshold (7 dB)
./wpan-capture-sim \
  --mode=per-sinr-sweep \
  --betaDb=7 \
  --txPowerDbm=-20 \
  --noiseFigureDb=10 \
  --bandwidthHz=2000000 \
  --out=../../out/calibration

# Capture margin test
./wpan-capture-sim \
  --mode=capture-test \
  --captureDb=6 \
  --betaDb=7 \
  --txPowerDbm=-20 \
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

**Resolved:**
- ✅ **Transmit Power Control:** Fixed using `LrWpanSpectrumValueHelper`. Can now set arbitrary transmit power in dBm and achieve desired SINR ranges. See **plan/PHASE_1_POWER_FIX_RESULTS.md** for implementation details.

**Remaining:**
1. **Low Packet Counts:** Only ~6 packets are transmitted per test instead of the configured 400, limiting statistical significance.
   - Likely causes: IPv6 neighbor discovery delays, UDP Echo timing, MAC initialization
   - **Workaround:** Increase simulation duration or switch to OnOff application
   - **Impact:** Cannot measure PER statistically (need 400+ packets)

**Current Status:**
- SINR control working: Can achieve 2-18 dB range with -20 dBm transmit power (spans beta threshold of 7 dB)
- Per-node power setting working: Capture test can set different powers on interferers
- Need to fix packet count to observe PER variation and complete Phase 1 DoD

See **plan/PHASE_1_POWER_FIX_RESULTS.md** for latest results and next steps.

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