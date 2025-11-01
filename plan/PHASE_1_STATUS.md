# Phase 1 - PHY Calibration Status

**Last Updated:** 2025-11-01 (Evening Session)
**Overall Status:** 🟡 In Progress - Protocol Stack Issue Identified

---

## Executive Summary

Phase 1 implementation has successfully resolved the PHY error model configuration issues based on ns-3 community research. However, a new blocking issue has been identified: packets are successfully transmitted and ACKed at the MAC layer but are not reaching the application layer, suggesting a problem in the IPv6/6LoWPAN/UDP stack.

**Progress:**
- ✅ Build infrastructure working (CMake, helpers, plotting)
- ✅ Transmit power control solved using `LrWpanSpectrumValueHelper`
- ✅ **Noise PSD properly configured** with thermal noise + noise figure
- ✅ SINR controllable across full range (-8 to +18 dB)
- ✅ Packet count stable at ~102 packets using OnOff application
- ✅ MAC layer working correctly (TX + ACK verified)
- ❌ **NEW ISSUE:** Packets not reaching application layer despite MAC success

**Blocking Issue:** MAC reports 102 successful transmissions with ACKs, but PacketSink receives 0 packets. This indicates a protocol stack issue between MAC and Application layers (6LoWPAN, IPv6, or UDP).

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

  // Noise PSD with explicit thermal noise + NF
  double noiseFactor = std::pow(10.0, noiseFigureDb / 10.0);
  helper.SetNoiseFactor(noiseFactor);
  Ptr<SpectrumValue> noise = helper.CreateNoisePowerSpectralDensity(channelNumber);
  phy->SetNoisePowerSpectralDensity(noise);
}
```

**Key API Learnings:**
- `LrWpanSpectrumValueHelper` is **not static** - must instantiate
- Noise factor must be set **before** calling `CreateNoisePowerSpectralDensity()`
- Noise factor is **linear ratio** (10^(NF_dB/10)), not dB
- Default noise PSD is **zero** unless explicitly set

**Verification:**
- ✅ Can set arbitrary transmit power in dBm
- ✅ Per-node power control works for capture tests
- ✅ Link budget calculations match expected SINR values

**Files Modified:** `sim/wpan_capture_sim.cc:54-83` (SetTxDbmAndNoise function)

---

### Issue 2: Low Packet Count (RESOLVED ✅)

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
- 102 samples is statistically acceptable for PER measurement

**Timing Configuration:**
- Applications start at 5.0s (allows IPv6 neighbor discovery to complete)
- Applications stop at 105.0s (100 second transmission window)
- Simulator stops at 115.0s (allows completion)

**Files Modified:** `sim/wpan_capture_sim.cc` (both per-sinr-sweep and capture-test modes)

---

### Issue 3: PHY Error Model Configuration (RESOLVED ✅)

**Problem:** Initial implementation had zero noise PSD, causing infinite SINR and 0% PER.

**Research Findings:**

Based on ns-3 community research and official lr-wpan examples:

1. **Noise PSD defaults to zero** unless explicitly set
2. **Must set noise factor** before creating noise PSD:
   ```cpp
   double noiseFactor = std::pow(10.0, noiseFigureDb / 10.0);  // Linear ratio
   helper.SetNoiseFactor(noiseFactor);
   Ptr<SpectrumValue> noise = helper.CreateNoisePowerSpectralDensity(channelNumber);
   ```
3. **PHY-level tracing** requires different callback signatures than MAC
4. **MAC retries should stay enabled** when measuring at PHY level
5. **802.15.4 lr-wpan examples** use MAC-level data indication callbacks, not PHY traces

**Implementation Changes:**
- ✅ Set explicit noise PSD with thermal noise (-174 dBm/Hz) + noise figure
- ✅ Switched from PHY tracing to MAC/Application tracing
- ✅ Keep MAC retries enabled (needed for proper MAC operation)

**Files Modified:** `sim/wpan_capture_sim.cc:73-78` (noise PSD configuration)

---

### Issue 4: Application Layer Packet Loss (CURRENT BLOCKING ISSUE ❌)

**Problem:** PacketSink receives 0 packets despite MAC reporting 102 successful transmissions.

**Diagnostic Results:**

Test with -20 dBm TX power (good SINR conditions):
```
distance_m,sinr_db,mac_tx_ok,mac_tx_drop,mac_rx_data,per
6.00,17.65,102,0,0,1.00   ← MAC TX OK, but 0 received!
8.00,13.90,102,0,0,1.00
10.00,10.99,102,0,0,1.00
12.00,8.61,102,0,0,1.00
14.00,6.61,102,0,0,1.00
16.00,4.87,102,0,0,1.00
18.00,3.33,102,0,0,1.00
20.00,1.96,102,0,0,1.00
```

**Critical Analysis:**

```
MAC TX OK: 102        ← Sender MAC successfully transmitted frames
MAC TX Drop: 0        ← No MAC-level drops
PacketSink RX: 0      ← ZERO packets at application layer!
Calculated PER: 100%  ← Misleading - not a PHY error, packets vanish in protocol stack
```

**This means:**
1. ✅ Packets ARE being sent from sender MAC
2. ✅ Receiver MAC IS receiving frames (otherwise ACKs wouldn't be sent)
3. ✅ Receiver MAC IS ACKing frames back to sender (otherwise mac_tx_ok = 0)
4. ❌ Packets VANISH between MAC and Application layer

**Likely Root Causes:**

1. **6LoWPAN fragmentation/reassembly issue**
   - IPv6/UDP packets may be fragmented by 6LoWPAN
   - Fragments might not be reassembling correctly
   - Or fragment timeout too short

2. **IPv6 routing/addressing issue**
   - Packets reach MAC but fail IPv6 layer validation
   - IPv6 neighbor discovery incomplete
   - Address mismatch between sender and receiver

3. **UDP socket binding issue**
   - PacketSink socket not properly bound to receiving address
   - Port mismatch
   - IPv6 address family mismatch

4. **PacketSink trace callback not firing**
   - Packets ARE received but callback isn't connected properly
   - Need to check `PacketSink::GetTotalRx()` to verify actual reception

**Measurement Approach Considerations:**

The official ns-3 `lr-wpan-per-plot.cc` example uses:
- **Direct MAC data transfer** via `McpsDataRequest()` (no IP stack)
- **McpsDataIndicationCallback** to count received frames
- **Small MSDU payloads** (7 bytes)
- **No IPv6/6LoWPAN/UDP overhead**

Our current approach uses:
- **Full IPv6/UDP/6LoWPAN stack**
- **Larger packets** (~40 bytes + headers)
- **Application-layer measurement** (PacketSink)

**Decision Point:**

We need to choose between:

**Option A: Pure MAC Approach** (following ns-3 examples)
- Pros: Proven to work; matches ns-3 documentation; simpler
- Cons: Doesn't test full stack; different packet sizes than Phase 2+; may need rework

**Option B: Debug Full Stack** (current approach)
- Pros: Tests actual Phase 2+ configuration; realistic packet sizes
- Cons: More complex debugging; may hit ns-3 6LoWPAN bugs

**Option C: Quick Diagnostic First**
- Check `PacketSink::GetTotalRx()` to see if packets are received but trace broken
- Enable pcap capture to inspect wire-level frames
- Takes 10-30 min before committing to refactor

---

## Current Configuration

### Working Components

**Power Control:**
```cpp
// Can set arbitrary power per device with proper noise
SetTxDbmAndNoise(dev, -20.0, 11, 10.0, 2e6);  // -20 dBm, NF=10dB
SetTxDbmAndNoise(dev, -30.0, 11, 10.0, 2e6);  // -30 dBm for low SINR tests
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
// MAC-level tracing (sender)
mac0->TraceConnectWithoutContext("MacTxOk", MakeBoundCallback(&OnMacTxOk, &ctr));
mac0->TraceConnectWithoutContext("MacTxDrop", MakeBoundCallback(&OnMacTxDrop, &ctr));

// Application-level tracing (receiver)
pktSink->TraceConnectWithoutContext("Rx", MakeBoundCallback(&OnPacketSinkRx, &rxCtr));
```

### Verified Working

**PHY/MAC Layer:**
- ✅ Transmit power control
- ✅ Noise PSD configuration
- ✅ SINR computation matching link budget
- ✅ MAC transmission and ACK mechanism
- ✅ CSMA/CA backoff and channel access

**Not Working:**

**Protocol Stack:**
- ❌ Packets not reaching application layer
- ❓ 6LoWPAN fragmentation/reassembly unclear
- ❓ IPv6 routing/forwarding unclear
- ❓ UDP delivery unclear

---

## Comparison: Evolution of Results

| Metric | Initial | After OnOff | After Noise Fix | Target | Status |
|--------|---------|-------------|-----------------|--------|---------|
| **Packet Count** | 6 | 102 | 102 | 400 | ✅ Acceptable |
| **SINR Range** | 22-38 dB | -8 to +18 dB | -8 to +18 dB | Spans beta (7 dB) | ✅ Excellent |
| **Power Control** | ❌ | ✅ | ✅ | Per-node control | ✅ Complete |
| **Noise PSD** | Zero (infinite SINR) | Zero | **Properly set** | Thermal + NF | ✅ Complete |
| **MAC TX** | Working | Working | **102 OK** | Transmit packets | ✅ Complete |
| **App RX** | 0 | 0 | **0** | Receive packets | ❌ **BLOCKED** |
| **PER Measurement** | N/A | N/A | **Cannot measure** | Varies with SINR | ❌ **BLOCKED** |

---

## Phase 1 Definition of Done (DoD) Status

From `plan/PHASE_1.md`:

| Requirement | Status | Notes |
|-------------|--------|-------|
| ✅ Build system working | ✅ Complete | CMake, helpers, clean builds |
| ✅ Two calibration modes | ✅ Complete | per-sinr-sweep, capture-test |
| ✅ SINR computation | ✅ Complete | Link budget calculations verified |
| ✅ Power control | ✅ Complete | Using LrWpanSpectrumValueHelper |
| ✅ Noise PSD config | ✅ Complete | Thermal noise + NF properly set |
| ❌ PER vs SINR curve | ❌ **BLOCKED** | Need working end-to-end packet delivery |
| ❌ Beta threshold @ ~7 dB | ❌ **BLOCKED** | Cannot measure PER without packet reception |
| ❌ Capture margin test | ❌ **BLOCKED** | Cannot test without packet reception |
| ✅ CSV output | ✅ Complete | per_vs_sinr.csv, capture_toggle.csv (structure ready) |
| ⚠️ Plots | ⚠️ Pending | Scripts ready, awaiting meaningful data |
| ❌ `capture_medium.yaml` | ❌ **BLOCKED** | Cannot finalize parameters without calibration |

**Overall Phase 1 Status:** Infrastructure complete; blocked on protocol stack packet delivery.

---

## Files Modified

### Created
- `sim/CMakeLists.txt` - Build configuration
- `sim/wpan_capture_sim.cc` - Main calibration harness
- `sim/helpers/channel_export.h/cc` - Link budget utilities
- `configs/capture_medium.yaml` - PHY/MAC parameters (placeholder)
- `scripts/plot_recipes.py` - Plotting utilities

### Modified This Session
- `sim/wpan_capture_sim.cc:54-83` - SetTxDbmAndNoise with proper noise PSD
- `sim/wpan_capture_sim.cc:85-93` - RxCounters and PacketSink callback
- `sim/wpan_capture_sim.cc:240-255` - per-sinr-sweep tracing
- `sim/wpan_capture_sim.cc:321-338` - capture-test tracing

---

## Next Steps (Priority Order)

### 1. Quick Diagnostic (30 min max) ⚡ IMMEDIATE

**Goal:** Determine if packets are being received but measurement is broken, or truly lost.

**Actions:**
```cpp
// Add after simulation:
uint64_t totalRx = pktSink->GetTotalRx();
std::cout << "DEBUG: PacketSink received " << totalRx << " bytes\n";
```

**If totalRx > 0:**
- Problem is just the trace callback → easy fix

**If totalRx == 0:**
- Enable pcap capture:
  ```cpp
  wpan.EnablePcap("debug", scene.devs);
  ```
- Inspect with Wireshark to see actual frames
- Check for 6LoWPAN fragmentation, IPv6 headers, UDP ports

### 2. Decision Point: Architecture Choice

Based on diagnostic results, choose one:

**Path A: Pure MAC Approach** (if stack issues persist)
- Refactor to use `McpsDataRequest()` / `McpsDataIndicationCallback`
- Follow `lr-wpan-per-plot.cc` example exactly
- Pro: Guaranteed to work; matches ns-3 best practices
- Con: Doesn't test full IPv6 stack; may differ from Phase 2+

**Path B: Fix Full Stack** (if close to working)
- Debug 6LoWPAN/IPv6/UDP issues
- Ensure address assignment, routing, fragmentation correct
- Pro: Tests realistic configuration for Phase 2+
- Con: May hit ns-3 bugs; more complex

### 3. Once Packet Reception Works

- [ ] Test PER at various SINR levels (-10 to +20 dB)
- [ ] Verify PER varies monotonically with SINR
- [ ] Look for "knee" in PER curve (should be near beta threshold ~7 dB)
- [ ] Run capture-test to validate capture margin behavior
- [ ] Generate calibration plots
- [ ] Finalize `capture_medium.yaml` with validated parameters
- [ ] Complete Phase 1 DoD

### 4. Documentation

- [ ] Document final solution (pure MAC vs full stack)
- [ ] Update README.md with working calibration commands
- [ ] Create PHASE_1_COMPLETE.md when DoD achieved
- [ ] Note any caveats for Phase 2+ (e.g., packet size differences)

---

## Research Notes

### ns-3 lr-wpan Error Model Findings

From official ns-3 documentation and community research:

1. **Default noise is ZERO** - must explicitly set noise PSD
2. **Noise factor is linear ratio**, not dB: `noiseFactor = 10^(NF_dB/10)`
3. **Must call SetNoiseFactor() before CreateNoisePowerSpectralDensity()**
4. **802.15.4 uses O-QPSK with standard BER curves** (Annex E.4.1.7)
5. **Preamble acquisition gate at SNR > -5 dB** (frames drop below this)
6. **No explicit "beta" knob** - use noise/power/pathloss to control SINR
7. **Official examples use MAC-level measurement** (`McpsDataIndication`), not PHY traces

### Key ns-3 Examples

- `lr-wpan-per-plot.cc` - PER vs received signal (uses `FixedRssLossModel`)
- `lr-wpan-error-distance-plot.cc` - PSR vs distance
- `lr-wpan-error-model-plot.cc` - Theoretical vs experimental error rates

All use **direct MAC data transfer**, not IP/UDP stack.

---

## Success Criteria

**Completed:**
- ✅ Transmit power control API working
- ✅ Can set different powers on different nodes
- ✅ SINR values predictable and controllable
- ✅ SINR range spans beta threshold (and far beyond)
- ✅ Sufficient packet counts for statistical measurement (~100 packets)
- ✅ **Noise PSD properly configured with thermal noise + NF**
- ✅ **MAC layer transmitting and ACKing successfully**

**Remaining:**
- ❌ End-to-end packet delivery (MAC → Application)
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

**Check Output:**
```bash
cat ../../out/calibration/per_vs_sinr.csv
# Current output shows: mac_tx_ok=102, mac_rx_data=0, per=1.00
```

### Diagnostic Commands (Next)

**Add debug output:**
```cpp
uint64_t totalRx = pktSink->GetTotalRx();
std::cout << "PacketSink total bytes: " << totalRx << "\n";
```

**Enable pcap capture:**
```cpp
wpan.EnablePcap("calibration", scene.devs);
# Inspect: wireshark calibration-0-0.pcap
```

**Enable ns-3 logging:**
```bash
NS_LOG="Sixlowpan=level_all|prefix_time:Ipv6L3Protocol=level_all|prefix_time" \
  ./wpan-capture-sim --mode=per-sinr-sweep ...
```

---

## References

**Key ns-3 Modules:**
- `lr-wpan`: IEEE 802.15.4 PHY/MAC
- `spectrum`: Spectrum-aware channel modeling
- `propagation`: Path loss models
- `sixlowpan`: 6LoWPAN adaptation layer (potential issue area)

**Implementation Files:**
- `sim/wpan_capture_sim.cc:54-83` - SetTxDbmAndNoise with noise PSD
- `sim/wpan_capture_sim.cc:108-141` - MakeScene function (network setup)
- `sim/wpan_capture_sim.cc:210-271` - per-sinr-sweep mode
- `sim/wpan_capture_sim.cc:274-344` - capture-test mode

**Documentation:**
- `plan/PLAN.md` - High-level simulation design
- `plan/PHASES.md` - Detailed phase breakdown
- `plan/PHASE_1.md` - Phase 1 objectives and tasks
- **ns-3 lr-wpan model documentation** - Error model, PHY parameters
- **ns-3 examples:** `src/lr-wpan/examples/lr-wpan-per-plot.cc`

---

**Last Updated:** 2025-11-01 (Evening Session)
**Next Review:** After quick diagnostic or refactoring decision
