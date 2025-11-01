# Problem Statement: ns-3 LrWpanPhy Transmit Power Control

**STATUS: ✅ RESOLVED** - See PHASE_1_POWER_FIX_RESULTS.md for solution

---

## Context

We are implementing Phase 1 of an IEEE 802.15.4 (lr-wpan) network simulator using **ns-3 (latest from GitLab main branch, ns-3-dev)** on a SpectrumChannel. The goal is to calibrate the PHY layer by measuring Packet Error Rate (PER) as a function of SINR, and to validate capture margin behavior in a three-node interference test. To achieve this, we need to control the transmit power of individual nodes to create specific SINR conditions at receivers.

## The Problem

The ns-3 `LrWpanPhy` class does not appear to expose a straightforward method to set transmit power in dBm. Our attempts to control transmit power have failed:

1. **Attempted:** `dev->GetPhy()->SetAttribute("TxPower", DoubleValue(txPowerDbm))`
   - **Result:** Runtime fatal error: `"Attribute name=TxPower does not exist for this object: tid=ns3::lrwpan::LrWpanPhy"`

2. **Investigated:** Searching the `LrWpanPhy` class reveals it uses **spectral power density** (Watts/Hz) rather than total transmit power (dBm)
   - Method exists: `SetTxPowerSpectralDensity(Ptr<SpectrumValue> txPsd)`
   - Unclear: How to properly construct the `SpectrumValue` object for a given total power in dBm

3. **Current workaround:** Using default transmit power, which results in very high SINR values (22-38 dB) across our test distances (6-20m with LogDistancePropagationLossModel, exponent=3.0, referenceLoss=40dB). This prevents us from observing PER variation near our target beta threshold of ~7 dB.

## What We Need

We need a working code example showing how to **set the transmit power of an `LrWpanNetDevice`** to a specific value in dBm (e.g., 0 dBm, -3 dBm, +3 dBm) on a `SingleModelSpectrumChannel` with IEEE 802.15.4 OQPSK PHY (2.4 GHz, ~2 MHz bandwidth). Specifically:

- How to correctly instantiate and populate a `SpectrumValue` object for a given total power budget (in dBm) and bandwidth (in Hz)
- Whether `SetTxPowerSpectralDensity()` is the correct approach, or if there's a higher-level helper or attribute we should be using
- How this interacts with the SpectrumChannel's propagation loss models and the receiver's error model

Ideally, we want to be able to set different transmit powers on different nodes (for the capture margin test) and have this result in predictable received SINR values that we can verify against our link budget calculations.

## Relevant Code Context

```cpp
// Our current setup (simplified)
Ptr<SingleModelSpectrumChannel> channel = CreateObject<SingleModelSpectrumChannel>();
Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
loss->SetReference(1.0, 40.0);  // 1m reference, 40dB loss
loss->SetPathLossExponent(3.0);
channel->AddPropagationLossModel(loss);

LrWpanHelper wpan;
wpan.SetChannel(channel);
NetDeviceContainer devs = wpan.Install(nodes);

// What we want to do (but doesn't work):
// dev->GetPhy()->SetAttribute("TxPower", DoubleValue(0.0));  // FAILS

// What we think might work but don't know how to implement:
// Ptr<SpectrumValue> txPsd = ???  // How to create this for 0 dBm total power?
// dev->GetPhy()->SetTxPowerSpectralDensity(txPsd);
```

## Expected Outcome

A code snippet or guidance that allows us to:
1. Set node A's transmit power to 0 dBm
2. Set node B's transmit power to -6 dBm
3. Verify that the resulting SINR at a receiver (accounting for distance-based path loss) matches our link budget calculations within a few dB

This will enable us to complete the PHY calibration by testing PER across a range of SINR values near the demodulation threshold.
