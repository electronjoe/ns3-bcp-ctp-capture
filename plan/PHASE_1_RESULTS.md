# PHASE_1_RESULTS.md — PHY Calibration Results

**Date:** 2025-11-01
**Status:** Partially Complete (with issues to address)

---

## Summary

Phase 1 focused on establishing a calibrated 802.15.4 PHY on SpectrumChannel and validating PER vs SINR behavior with capture margin testing. The core infrastructure has been successfully implemented and can execute both calibration modes.

---

## Artifacts Created

### Code
- ✅ `sim/CMakeLists.txt` - Build configuration with ns-3 linkage
- ✅ `sim/wpan_capture_sim.cc` - Main calibration harness with two modes
- ✅ `sim/helpers/channel_export.h/.cc` - Link budget computation utilities
- ✅ `configs/capture_medium.yaml` - PHY/MAC parameter configuration
- ✅ `scripts/plot_recipes.py` - Plotting utilities for calibration data

### Data
- ✅ `out/calibration/per_vs_sinr.csv` - PER vs SINR sweep data
- ✅ `out/calibration/per_vs_sinr.png` - PER vs SINR plot
- ✅ `out/calibration/capture_toggle.csv` - Capture margin test data
- ✅ `out/calibration/capture_toggle.png` - Capture margin plot

---

## Results Analysis

### per-sinr-sweep Mode

**Observations:**
- SINR range: 21.96 dB to 37.65 dB (across distances 6m to 20m)
- PER: 0.00 for all test points
- Packets per test: 6 (expected: 400)

**Issues:**
1. **Low packet count:** Only 6 packets transmitted instead of configured 400
   - Likely cause: UDP Echo timing or application stop time issue
   - Impact: Insufficient samples for reliable PER measurement

2. **High SINR values:** All SINR values well above typical 802.15.4 sensitivity
   - Cause: Default ns-3 LrWpanPhy transmit power settings are high
   - Impact: Cannot observe PER "knee" behavior near beta threshold

3. **Zero PER:** No packet drops observed
   - Expected: Should see increasing PER as SINR decreases
   - Cause: SINR too high for distances tested

### capture-test Mode

**Observations:**
- Power delta range: -8 dB to +8 dB
- PER: 0.00 for all configurations
- Packets per test: 6 (expected: 400)

**Issues:**
1. **No capture effect observed:** PER remains 0 regardless of power differential
   - Expected: Should see PER variation around captureDb = 6 dB
   - Likely causes:
     - Packet count too low to observe statistical effects
     - Transmit power not being adjusted (code limitation noted)
     - Nodes too close (8m) relative to transmit power levels

---

## Definition of Done (DoD) Status

From PHASE_1.md:

| Criterion | Status | Notes |
|-----------|--------|-------|
| PER(SINR) curve shows knee near betaDb | ❌ | Cannot verify - SINR values too high, no PER variation |
| Monotonic PER decrease as SINR increases | ⚠️ | Trivially true (all zeros) but not meaningful |
| Capture test toggles success around captureDb | ❌ | No variation observed in PER |
| `capture_medium.yaml` committed | ✅ | File created and in use |

**Overall DoD:** **NOT MET** - Key calibration objectives not achieved

---

## Root Cause Analysis

### 1. Transmit Power Control
The original PHASE_1.md code used:
```cpp
dev->GetPhy()->SetTransmitPower(cfg.txPowerDbm);
```

However, ns-3 `LrWpanPhy` does not have a `SetTransmitPower` method or `TxPower` attribute. Our implementation currently:
- Comments out power setting (line 93-95 in wpan_capture_sim.cc)
- Uses default ns-3 LrWpanPhy transmit power spectral density

**Impact:** Cannot control signal strength, leading to very high SINR values.

### 2. UDP Echo Packet Count
Configured `MaxPackets=400` with `Interval=10ms` should send packets over 4 seconds. Only 6 packets sent suggests:
- Neighbor discovery delay consuming time
- Application stopping prematurely
- MAC layer contention or backoff preventing transmissions

---

## Next Steps to Complete Phase 1

### Priority 1: Fix Transmit Power Control
- Research correct ns-3 API for 802.15.4 transmit power
- Options:
  1. Use `SetTxPowerSpectralDensity()` with proper power/bandwidth calculation
  2. Adjust propagation model parameters instead (referenceLossDb, exponent)
  3. Increase test distances significantly (e.g., 50m-200m range)

### Priority 2: Fix Packet Count Issue
- Increase simulation duration (e.g., 30s instead of 10-12s)
- Add logging to understand why only 6 packets sent
- Consider direct MAC-level packet injection instead of UDP Echo

### Priority 3: Adjust SINR Range
Once transmit power is controlled or distances increased:
- Target SINR sweep from ~0 dB to ~15 dB to span the beta threshold
- Verify path loss calculations match expected values
- Adjust propagation model parameters (referenceLossDb, exponent)

### Priority 4: Validate Capture Mechanism
- Verify 3-node interference scenario is correctly set up
- Ensure concurrent transmissions are actually occurring
- May need to reduce MAC backoff to force collisions

---

## Recommended Approach for Next Session

**Option A: Distance-Based Calibration (Simpler)**
- Keep default transmit power
- Use much larger distances (100m-500m range)
- Adjust propagation model to create lower SINR values
- Easier to implement, avoids transmit power API issues

**Option B: Power-Based Calibration (More Realistic)**
- Invest time to understand ns-3 LrWpanPhy power control API
- Implement proper SetTxPowerSpectralDensity() calls
- Allows finer control over SINR
- More aligned with original plan but requires API research

**Recommendation:** Start with **Option A** to quickly achieve Phase 1 DoD, then implement Option B for proper power control in later phases.

---

## Files Modified

```
sim/
├─ CMakeLists.txt                 [created]
├─ wpan_capture_sim.cc            [created, ~262 lines]
└─ helpers/
   ├─ channel_export.h            [created]
   └─ channel_export.cc           [created]

configs/
└─ capture_medium.yaml            [created]

scripts/
└─ plot_recipes.py                [created]

out/calibration/
├─ per_vs_sinr.csv                [generated]
├─ per_vs_sinr.png                [generated]
├─ capture_toggle.csv             [generated]
└─ capture_toggle.png             [generated]

plan/
└─ PHASE_1_RESULTS.md             [this file]
```

---

## Commit Recommendation

While Phase 1 DoD is not fully met, significant infrastructure has been built. Recommend committing current state with clear documentation of limitations:

```bash
git add sim/ configs/ scripts/ plan/PHASE_1_RESULTS.md
git commit -m "Phase 1 (partial): PHY calibration harness and infrastructure

- Implemented wpan_capture_sim with per-sinr-sweep and capture-test modes
- Created CMake build system with ns-3 linkage
- Added channel_export helpers and plot_recipes utilities
- Generated initial calibration data and plots

KNOWN ISSUES (to address):
- Transmit power control not working (ns-3 API mismatch)
- Low packet counts (6 instead of 400)
- SINR values too high to observe PER variation
- No capture effect observed in 3-node test

See plan/PHASE_1_RESULTS.md for details and next steps."
```

---

**End of PHASE_1_RESULTS.md**
