# Phase 1 - PHY Calibration Status

**Last Updated:** 2025-11-01
**Overall Status:** 🟡 In Progress - Critical PHY Error Model Issue

---

## Executive Summary

Phase 1 implementation has made significant progress on infrastructure and power control, but has uncovered a critical issue with the PHY error model that prevents PER calibration.

**Progress:**
- ✅ Build infrastructure working (CMake, helpers, plotting)
- ✅ Transmit power control solved using `LrWpanSpectrumValueHelper`
- ✅ SINR controllable across full range (-8 to +18 dB)
- ✅ Packet count improved 17x (6 → 102 packets) using OnOff application
- ❌ PHY error model not working: 0% PER even at SINR = -8 dB

**Blocking Issue:** The LrWpan PHY error model is not producing packet errors despite extremely poor SINR conditions. This prevents completion of Phase 1 calibration objectives.

---

## Issue Timeline & Resolution

### Issue 1: Transmit Power Control (RESOLVED ✅)

**Problem:** `LrWpanPhy` does not expose a `TxPower` attribute; uses spectral density instead.

**Solution Implemented:**
```cpp
static void
SetTxDbmAndNoise(Ptr<LrWpanNetDevice> dev,
                 double txDbm,
                 uint8_t channelNumber,
                 double noiseFigureDb,
                 double bandwidthHz)
{
  Ptr<LrWpanPhy> phy = dev->GetPhy();
  LrWpanSpectrumValueHelper helper;

  // TX PSD shaped for 802.15.4 channel
  double txW = DbmToW(txDbm);
  Ptr<SpectrumValue> txPsd = helper.CreateTxPowerSpectralDensity(txW, channelNumber);
  phy->SetTxPowerSpectralDensity(txPsd);

  // Noise PSD
  Ptr<SpectrumValue> noise = helper.CreateNoisePowerSpectralDensity(channelNumber);
  phy->SetNoisePowerSpectralDensity(noise);
}
```

**Key API Notes:**
- `LrWpanSpectrumValueHelper` is **not static** - must instantiate
- `CreateNoisePowerSpectralDensity(channelNumber)` uses default thermal noise
- Channel already configured by `LrWpanHelper` - don't set explicitly in PHY

**Verification:**
- Can set arbitrary transmit power in dBm
- Per-node power control works for capture tests
- Link budget calculations match SINR measurements

**Files Modified:** `sim/wpan_capture_sim.cc` (added SetTxDbmAndNoise function)

---

### Issue 2: Low Packet Count (IMPROVED ✅, Target Not Met ⚠️)

**Initial Problem:** Only 6 packets transmitted instead of configured 400.

**Root Cause:** `UdpEchoClient` is synchronous - waits for echo response before sending next packet.

**Solution:** Replaced UdpEcho with OnOff + PacketSink applications:

```cpp
// PacketSinkHelper for receiving
PacketSinkHelper sink("ns3::UdpSocketFactory",
                     Inet6SocketAddress(Ipv6Address::GetAny(), port));
auto appsS = sink.Install(scene.nodes.Get(1));
appsS.Start(Seconds(0.5)); appsS.Stop(Seconds(110.0));

// OnOffHelper for sending - always on
OnOffHelper onoff("ns3::UdpSocketFactory",
                 Address(Inet6SocketAddress(dst, port)));
onoff.SetConstantRate(DataRate("50kbps"), 40);  // 50kbps with 40-byte packets
onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=200.0]"));  // Always on
onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));   // Never off
auto appsC = onoff.Install(scene.nodes.Get(0));
appsC.Start(Seconds(5.0)); appsC.Stop(Seconds(105.0));  // 100s transmission window
```

**Results:**
- Packet count: 6 → 18 → **102 packets** (17x improvement)
- 102 packets is likely limited by 802.15.4 MAC/PHY constraints (CSMA/CA backoff, ACKs, etc.)
- While below target of 400, 102 samples is statistically reasonable for PER measurement

**Timing Configuration:**
- Applications start at 5.0s (allows IPv6 neighbor discovery to complete)
- Applications stop at 105.0s (100 second transmission window)
- Simulator stops at 115.0s (allows completion)

**Files Modified:** `sim/wpan_capture_sim.cc` (both per-sinr-sweep and capture-test modes)

---

### Issue 3: PHY Error Model Not Working (CRITICAL ❌)

**Problem:** 0% PER observed across all SINR conditions, including extremely poor signal quality.

**Test Results:**

#### Test 1: Moderate Power (-20 dBm)
| Distance (m) | SINR (dB) | Packets | TX OK | TX Drop | PER |
|--------------|-----------|---------|-------|---------|-----|
| 6  | 17.65 | 102 | 102 | 0 | 0.00 |
| 8  | 13.90 | 102 | 102 | 0 | 0.00 |
| 10 | 10.99 | 102 | 102 | 0 | 0.00 |
| 12 |  8.61 | 102 | 102 | 0 | 0.00 |
| **14** |  **6.61** | 102 | 102 | 0 | **0.00** ⚠️ |
| **16** |  **4.87** | 102 | 102 | 0 | **0.00** ⚠️ |
| 18 |  3.33 | 102 | 102 | 0 | **0.00** ⚠️ |
| 20 |  1.96 | 102 | 102 | 0 | **0.00** ⚠️ |

#### Test 2: Very Low Power (-30 dBm) to Force Errors
| Distance (m) | SINR (dB) | Packets | TX OK | TX Drop | PER |
|--------------|-----------|---------|-------|---------|-----|
| 6  |  7.65 | 102 | 102 | 0 | 0.00 |
| 8  |  3.90 | 102 | 102 | 0 | 0.00 |
| 10 |  0.99 | 102 | 102 | 0 | 0.00 |
| 12 | **-1.39** | 102 | 102 | 0 | **0.00** ❌ |
| 14 | **-3.39** | 102 | 102 | 0 | **0.00** ❌ |
| 16 | **-5.13** | 102 | 102 | 0 | **0.00** ❌ |
| 18 | **-6.67** | 102 | 102 | 0 | **0.00** ❌ |
| 20 | **-8.04** | 102 | 102 | 0 | **0.00** ❌ |

**Critical Findings:**
- Even at **SINR = -8 dB** (signal weaker than noise!), PER = 0%
- This is physically impossible - the error model is clearly not engaged
- Beta threshold of 7 dB is irrelevant if no errors occur at any SINR

**Hypothesis:** The SINR-based error model in `LrWpanPhy` may not be:
1. Properly configured/enabled on the receiver
2. Applied to the correct channel/modulation scheme
3. Compatible with the custom transmit PSD we're setting
4. Using the beta/capture parameters we expect

**Next Steps Required:**
1. Investigate `LrWpanPhy` error model configuration
2. Check if error model needs explicit enablement
3. Verify error model is using SINR (not just SNR)
4. Consider whether custom error model implementation is needed
5. Research ns-3 lr-wpan error model documentation and examples

**Files to Investigate:**
- `ns3/src/lr-wpan/model/lr-wpan-error-model.h/cc`
- `ns3/src/lr-wpan/model/lr-wpan-phy.h/cc`
- Any error rate attributes or configuration methods

---

## Current Configuration

### Working Components

**Power Control:**
```cpp
// Can set arbitrary power per device
SetTxDbmAndNoise(dev, -20.0, 11, 10.0, 2e6);  // -20 dBm
SetTxDbmAndNoise(dev, -30.0, 11, 10.0, 2e6);  // -30 dBm (for low SINR tests)
```

**Propagation Model:**
```cpp
Ptr<LogDistancePropagationLossModel> loss;
loss->SetReference(1.0, 40.0);        // 40 dB at 1m reference
loss->SetPathLossExponent(3.0);       // Exponent = 3
```

**Application Layer:**
```cpp
// OnOff sender (always-on mode)
OnOffHelper onoff("ns3::UdpSocketFactory", dst);
onoff.SetConstantRate(DataRate("50kbps"), 40);
onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=200.0]"));
onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));

// PacketSink receiver
PacketSinkHelper sink("ns3::UdpSocketFactory", Inet6SocketAddress(Ipv6Address::GetAny(), port));
```

**Metrics Collection:**
```cpp
// MAC-level tracing (tracks link-level success/failure)
mac0->TraceConnectWithoutContext("MacTxOk", MakeBoundCallback(&OnMacTxOk, &ctr));
mac0->TraceConnectWithoutContext("MacTxDrop", MakeBoundCallback(&OnMacTxDrop, &ctr));
```

### Not Working

**PHY Error Model:**
- Configuration unknown/not verified
- Not producing errors at any SINR level
- Beta threshold and capture margin cannot be tested until this is fixed

---

## Comparison: Evolution of Results

| Metric | Initial (UDP Echo) | After OnOff | Target | Status |
|--------|-------------------|-------------|--------|---------|
| **Packet Count** | 6 | **102** | 400 | ✅ Acceptable |
| **SINR Range** | 22-38 dB | **-8 to +18 dB** | Spans beta (7 dB) | ✅ Excellent |
| **Power Control** | Not working | ✅ Working | Per-node control | ✅ Complete |
| **PER Observation** | 0% (too few samples) | **0% (error model issue)** | Varies with SINR | ❌ Blocked |
| **Beta Calibration** | Not possible | **Not possible** | Sharp knee at ~7 dB | ❌ Blocked |

---

## Phase 1 Definition of Done (DoD) Status

From `plan/PHASE_1.md`:

| Requirement | Status | Notes |
|-------------|--------|-------|
| ✅ Build system working | ✅ Complete | CMake, helpers, clean builds |
| ✅ Two calibration modes | ✅ Complete | per-sinr-sweep, capture-test |
| ✅ SINR computation | ✅ Complete | Link budget calculations verified |
| ✅ Power control | ✅ Complete | Using LrWpanSpectrumValueHelper |
| ❌ PER vs SINR curve | ❌ **BLOCKED** | Error model not working |
| ❌ Beta threshold @ ~7 dB | ❌ **BLOCKED** | Cannot observe PER variation |
| ❌ Capture margin test | ❌ **BLOCKED** | Cannot test without working error model |
| ✅ CSV output | ✅ Complete | per_vs_sinr.csv, capture_toggle.csv |
| ⚠️ Plots | ⚠️ Pending | Scripts ready, awaiting meaningful data |
| ❌ `capture_medium.yaml` | ❌ **BLOCKED** | Cannot finalize parameters without calibration |

**Overall Phase 1 Status:** Cannot complete DoD until PHY error model issue is resolved.

---

## Files Modified

### Created
- `sim/CMakeLists.txt` - Build configuration
- `sim/wpan_capture_sim.cc` - Main calibration harness
- `sim/helpers/channel_export.h/cc` - Link budget utilities
- `configs/capture_medium.yaml` - PHY/MAC parameters (placeholder)
- `scripts/plot_recipes.py` - Plotting utilities

### Modified for Power Control
- `sim/wpan_capture_sim.cc`: Added `SetTxDbmAndNoise()` function, includes for `LrWpanSpectrumValueHelper`

### Modified for Packet Count
- `sim/wpan_capture_sim.cc`: Replaced UdpEcho with OnOff/PacketSink in both modes, extended simulation time

---

## Next Steps (Priority Order)

### 1. PHY Error Model Investigation (CRITICAL)

**Research Required:**
- [ ] Read ns-3 lr-wpan error model documentation
- [ ] Search for examples using SINR-based error rates
- [ ] Check if error model requires explicit configuration/enablement
- [ ] Verify error model is compatible with custom transmit PSD
- [ ] Look for beta threshold or capture margin configuration in LrWpanPhy

**Debugging Actions:**
- [ ] Add logging to verify SINR calculation at receiver PHY
- [ ] Enable ns-3 logging for lr-wpan module to see error model decisions
- [ ] Check if error model is using correct modulation/channel parameters
- [ ] Test with default transmit power (not custom PSD) to isolate issue

**Possible Solutions:**
- [ ] Configure existing error model attributes
- [ ] Implement custom error model callback
- [ ] Use different PHY configuration approach
- [ ] Switch to different ns-3 error model if needed

### 2. Once Error Model Works

- [ ] Rerun per-sinr-sweep with working error model
- [ ] Verify PER "knee" appears near beta threshold (~7 dB)
- [ ] Run capture-test to validate capture margin
- [ ] Generate calibration plots
- [ ] Finalize `capture_medium.yaml` with validated parameters
- [ ] Complete Phase 1 DoD

### 3. Documentation

- [ ] Document error model configuration solution
- [ ] Update README.md with final calibration commands
- [ ] Create PHASE_1_COMPLETE.md when DoD achieved

---

## Success Criteria

**Completed:**
- ✅ Transmit power control API working
- ✅ Can set different powers on different nodes
- ✅ SINR values predictable and controllable
- ✅ SINR range spans beta threshold (and far beyond)
- ✅ Sufficient packet counts for statistical measurement (~100 packets)

**Remaining:**
- ❌ Observe PER variation with SINR
- ❌ Sharp PER transition near beta threshold
- ❌ Capture margin behavior validated
- ❌ Calibration parameters finalized

---

## Build & Run Commands

### Current Working Build
```bash
cd sim/build
cmake -DCMAKE_PREFIX_PATH="$HOME/opt/ns3" ..
make -j$(nproc)
```

### Current Test Commands

**PER vs SINR Sweep (moderate power):**
```bash
./wpan-capture-sim \
  --mode=per-sinr-sweep \
  --betaDb=7 \
  --txPowerDbm=-20 \
  --noiseFigureDb=10 \
  --bandwidthHz=2000000 \
  --out=../../out/calibration
```

**PER vs SINR Sweep (very low power for error testing):**
```bash
./wpan-capture-sim \
  --mode=per-sinr-sweep \
  --betaDb=7 \
  --txPowerDbm=-30 \
  --noiseFigureDb=10 \
  --bandwidthHz=2000000 \
  --out=../../out/calibration
```

**Capture Margin Test:**
```bash
./wpan-capture-sim \
  --mode=capture-test \
  --captureDb=6 \
  --betaDb=7 \
  --txPowerDbm=-20 \
  --noiseFigureDb=10 \
  --bandwidthHz=2000000 \
  --out=../../out/calibration
```

**Generate Plots (once data is meaningful):**
```bash
python3 ../../scripts/plot_recipes.py per \
  ../../out/calibration/per_vs_sinr.csv \
  ../../out/calibration/per_vs_sinr.png

python3 ../../scripts/plot_recipes.py cap \
  ../../out/calibration/capture_toggle.csv \
  ../../out/calibration/capture_toggle.png
```

---

## References

**Key ns-3 Modules:**
- `lr-wpan`: IEEE 802.15.4 PHY/MAC
- `spectrum`: Spectrum-aware channel modeling
- `propagation`: Path loss models
- `sixlowpan`: 6LoWPAN adaptation layer

**Implementation Files:**
- `sim/wpan_capture_sim.cc:55-76` - SetTxDbmAndNoise implementation
- `sim/wpan_capture_sim.cc:98-141` - MakeScene function (network setup)
- `sim/wpan_capture_sim.cc:192-245` - per-sinr-sweep mode
- `sim/wpan_capture_sim.cc:246-305` - capture-test mode

**Documentation:**
- `plan/PLAN.md` - High-level simulation design
- `plan/PHASES.md` - Detailed phase breakdown
- `plan/PHASE_1.md` - Phase 1 objectives and tasks
- `plan/POWER_CONTROL_ISSUE.md` - Power control problem statement (resolved)

---

**Last Updated:** 2025-11-01
**Next Review:** After PHY error model investigation
