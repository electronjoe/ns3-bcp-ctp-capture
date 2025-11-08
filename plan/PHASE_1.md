# Phase 1 — PHY Calibration DoD Guide
**Target audience:** the engineer finishing Phase 1 for *ns3-bcp-ctp-capture*.

This document reviews the current state, defines **Definition of Done (DoD)** for Phase 1, and gives a **step‑by‑step fix/implementation guide** to get there. It is designed for hand‑off.

> **Architecture reminder:** For calibration we want a tight control loop from generator → MAC → sink, without IPv6/6LoWPAN side effects. We will support *both* UDP/IPv6 and **PacketSocket (L2)**, but **PacketSocket** is the default for Phase 1 to guarantee sample counts and avoid neighbor‑discovery/stack delays.

---

## 0) What’s already in the repo (good news)
- ✅ CMake project builds `wpan-capture-sim`
- ✅ 802.15.4 PHY on `SpectrumChannel` with **LogDistance** propagation
- ✅ Correct **TX power/noise PSD** configuration via `LrWpanSpectrumValueHelper`
- ✅ **Two calibration modes** exist:
  - `per-sinr-sweep`
  - `capture-test`
- ✅ Plotting scripts: `scripts/plot_recipes.py`
- ✅ `configs/capture_medium.yaml` with sensible defaults

**Open issues blocking DoD:**
1. **Low packet counts** in calibration runs (observed ~6 instead of 400+), due to higher‑layer timing and discovery.
2. CSV fields compute PER using MAC TxOk/Drop, mixing retries with offered load → not the clean “PER vs SINR” we want.
3. Phase 1 requires repeatable PER curves with enough samples **per point** (≥ 400), and a clean **capture margin** toggle curve.

**Approach:** add a small **PacketSocket L2 generator + sink** app pair, and standardize CSVs. Keep UDP path as a secondary option.

---

## 1) Phase 1 Definition of Done (DoD)

**DoD‑1: PER vs SINR sweep**
- For distances in `configs/capture_medium.yaml.sweep.distances_m`, generate **≥ 400** test frames per point at L2.
- CSV `out/calibration/per_vs_sinr.csv` has columns:
  ```text
  distance_m,sinr_db,offered,received,per
  ```
  where `per = 1 - received/offered`.
- Plot `out/calibration/per_vs_sinr.png` shows a **knee near β ≈ 7 dB**, monotone decreasing PER vs SINR, with visible transition.

**DoD‑2: Capture margin test**
- With desired TX and one interferer near the RX, sweep Δ(dB) = P_int − P_des in e.g. `{-8,…,+8}`.
- CSV `out/calibration/capture_toggle.csv` has:
  ```text
  delta_db,offered,received,per
  ```
- Plot `out/calibration/capture_toggle.png` shows marked transition near **capture margin x ≈ 6 dB**.

**DoD‑3: Parameter freeze for later phases**
- Update `configs/capture_medium.yaml` with finalized `{betaDb, captureDb, referenceLossDb, exponent}` that best fit measured curves.
- Commit plots and CSVs under `out/calibration/` (or attach in PR).

---

## 2) Required code changes (overview)
1. **Add PacketSocket apps** for L2 calibration:
   - `PacketSprayerApp` — sends N frames at fixed interval to destination MAC (no dependencies on IP).
   - `RawL2SinkApp` — binds a PacketSocket and counts received frames.
2. **Standardize CSV schema** and PER computation (offered vs received).
3. **Switch calibration modes to PacketSocket by default**; leave UDP path as optional fallback (`--transport=packet|udp`).
4. Optional (recommended for PER purity): **disable MAC retries** in calibration (`maxFrameRetries=0`).

---

## 3) Step‑by‑step implementation guide

### 3.1 Create the L2 apps (new files)
Create directory and files:
```
sim/apps/
  packet_sprayer.h
  packet_sprayer.cc
  raw_l2_sink.h
  raw_l2_sink.cc
```

**`packet_sprayer.h`**
```cpp
#pragma once
#include "ns3/application.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/packet-socket-address.h"
#include "ns3/net-device.h"

namespace calib {

class PacketSprayerApp : public ns3::Application {
public:
  static ns3::TypeId GetTypeId (void);
  PacketSprayerApp();

  // Configure destination and pacing
  void Configure(ns3::Ptr<ns3::NetDevice> outDev,
                 ns3::Address destMac,
                 uint32_t pktSizeBytes,
                 uint32_t count,
                 ns3::Time interval);

  uint32_t Offered() const { return m_totalToSend; }
  uint32_t Sent() const { return m_sent; }

protected:
  void StartApplication() override;
  void StopApplication() override;

private:
  void SendOne();
  void ScheduleNext();

  ns3::Ptr<ns3::Socket> m_sock;
  ns3::Ptr<ns3::NetDevice> m_dev;
  ns3::PacketSocketAddress m_dst;
  uint32_t m_pktSize{40};
  uint32_t m_totalToSend{0};
  uint32_t m_sent{0};
  ns3::Time m_interval;
  ns3::EventId m_ev;
};

} // namespace calib
```

**`packet_sprayer.cc`**
```cpp
#include "packet_sprayer.h"
#include "ns3/packet-socket-factory.h"
#include "ns3/simulator.h"
#include "ns3/log.h"

namespace calib {
NS_LOG_COMPONENT_DEFINE("PacketSprayerApp");
NS_OBJECT_ENSURE_REGISTERED(PacketSprayerApp);

ns3::TypeId PacketSprayerApp::GetTypeId() {
  static ns3::TypeId tid = ns3::TypeId("calib::PacketSprayerApp")
    .SetParent<ns3::Application>()
    .AddConstructor<PacketSprayerApp>();
  return tid;
}
PacketSprayerApp::PacketSprayerApp() {}

void PacketSprayerApp::Configure(ns3::Ptr<ns3::NetDevice> outDev,
                                 ns3::Address destMac,
                                 uint32_t pktSizeBytes,
                                 uint32_t count,
                                 ns3::Time interval) {
  m_dev = outDev;
  m_pktSize = pktSizeBytes;
  m_totalToSend = count;
  m_interval = interval;
  m_dst.SetSingleDevice(outDev->GetIfIndex());
  m_dst.SetProtocol(0);
  m_dst.SetPhysicalAddress(destMac);
}

void PacketSprayerApp::StartApplication() {
  if (!m_sock) {
    m_sock = ns3::Socket::CreateSocket(GetNode(), ns3::PacketSocketFactory::GetTypeId());
    ns3::PacketSocketAddress bind;
    bind.SetSingleDevice(m_dev->GetIfIndex());
    bind.SetProtocol(0);
    m_sock->Bind(bind);
  }
  m_sent = 0;
  ScheduleNext();
}

void PacketSprayerApp::StopApplication() {
  if (m_ev.IsRunning()) ns3::Simulator::Cancel(m_ev);
  if (m_sock) { m_sock->Close(); m_sock = nullptr; }
}

void PacketSprayerApp::SendOne() {
  if (m_sent >= m_totalToSend) return;
  auto p = ns3::Create<ns3::Packet>(m_pktSize);
  m_sock->SendTo(p, 0, m_dst);
  ++m_sent;
  ScheduleNext();
}

void PacketSprayerApp::ScheduleNext() {
  if (m_sent < m_totalToSend) {
    m_ev = ns3::Simulator::Schedule(m_interval, &PacketSprayerApp::SendOne, this);
  }
}

} // namespace calib
```

**`raw_l2_sink.h`**
```cpp
#pragma once
#include "ns3/application.h"
#include "ns3/socket.h"

namespace calib {
class RawL2SinkApp : public ns3::Application {
public:
  static ns3::TypeId GetTypeId (void);
  RawL2SinkApp();
  uint32_t Received() const { return m_rx; }
protected:
  void StartApplication() override;
  void StopApplication() override;
private:
  void HandleRead(ns3::Ptr<ns3::Socket> sock);
  ns3::Ptr<ns3::Socket> m_sock;
  uint32_t m_rx{0};
};
}
```

**`raw_l2_sink.cc`**
```cpp
#include "raw_l2_sink.h"
#include "ns3/packet-socket-factory.h"
#include "ns3/packet-socket-address.h"
#include "ns3/log.h"

namespace calib {
NS_LOG_COMPONENT_DEFINE("RawL2SinkApp");
NS_OBJECT_ENSURE_REGISTERED(RawL2SinkApp);

ns3::TypeId RawL2SinkApp::GetTypeId() {
  static ns3::TypeId tid = ns3::TypeId("calib::RawL2SinkApp")
    .SetParent<ns3::Application>()
    .AddConstructor<RawL2SinkApp>();
  return tid;
}
RawL2SinkApp::RawL2SinkApp() {}

void RawL2SinkApp::StartApplication() {
  m_sock = ns3::Socket::CreateSocket(GetNode(), ns3::PacketSocketFactory::GetTypeId());
  ns3::PacketSocketAddress any;
  any.SetAllDevices();
  any.SetProtocol(0);
  m_sock->Bind(any);
  m_sock->SetRecvCallback(MakeCallback(&RawL2SinkApp::HandleRead, this));
}
void RawL2SinkApp::StopApplication() {
  if (m_sock) { m_sock->Close(); m_sock = nullptr; }
}
void RawL2SinkApp::HandleRead(ns3::Ptr<ns3::Socket> sock) {
  ns3::Ptr<ns3::Packet> p;
  ns3::Address from;
  while ((p = sock->RecvFrom(from))) { ++m_rx; }
}
}
```

### 3.2 Wire PacketSocket into the build
Update `sim/CMakeLists.txt` to compile the new files:
```cmake
add_executable(wpan-capture-sim
  wpan_capture_sim.cc
  helpers/channel_export.cc
  apps/packet_sprayer.cc
  apps/raw_l2_sink.cc
)
target_include_directories(wpan-capture-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wpan-capture-sim PRIVATE
  ns3::core ns3::network ns3::mobility ns3::internet ns3::applications
  ns3::lr-wpan ns3::sixlowpan ns3::spectrum ns3::propagation
  ns3::packet-socket
)
```

### 3.3 Expose a `--transport` flag (default `packet`)
In `sim/wpan_capture_sim.cc`:
- Add header includes:
  ```cpp
  #include "ns3/packet-socket-helper.h"
  #include "ns3/packet-socket-address.h"
  #include "apps/packet_sprayer.h"
  #include "apps/raw_l2_sink.h"
  ```
- Add CLI flag:
  ```cpp
  std::string transport = "packet"; // or "udp"
  cmd.AddValue("transport", "packet | udp", transport);
  ```

### 3.4 Disable MAC retries for calibration (optional but recommended)
Right after device creation in `MakeScene`:
```cpp
dev->GetMac()->SetAttribute("MaxFrameRetries", UintegerValue(0)); // for raw PER
dev->GetMac()->SetAttribute("AckWaitDuration", TimeValue(MilliSeconds(0))); // optional
```
Keep defaults for non‑calibration modes later.

### 3.5 Replace UDP apps with PacketSocket apps in calibration modes
**per‑sinr‑sweep (L2 path)**:
```cpp
if (transport == "packet") {
  using namespace calib;
  // L2 sink on receiver
  auto sink = CreateObject<RawL2SinkApp>();
  scene.nodes.Get(1)->AddApplication(sink);
  sink->SetStartTime(Seconds(0.5));
  sink->SetStopTime(Seconds(30.0));

  // L2 sprayer on sender
  auto sprayer = CreateObject<PacketSprayerApp>();
  auto dev0 = DynamicCast<lrwpan::LrWpanNetDevice>(scene.devs.Get(0));
  auto dev1 = DynamicCast<lrwpan::LrWpanNetDevice>(scene.devs.Get(1));
  sprayer->Configure(dev0, dev1->GetAddress(), /*pktSize*/40,
                     /*count*/ cfg.packetsPerPoint, /*interval*/ MilliSeconds(2)); // ~500 pps
  scene.nodes.Get(0)->AddApplication(sprayer);
  sprayer->SetStartTime(Seconds(5.0));
  sprayer->SetStopTime(Seconds(25.0));
  // After run: offered = sprayer->Sent(), received = sink->Received()
} else {
  // keep existing UDP OnOff + PacketSink path
}
```
**capture‑test (L2 path)** is analogous, plus the interferer gets its own `PacketSprayerApp` towards the RX.

### 3.6 Fix CSV fields (both modes)
- Replace MAC‑based counts with **offered** (sprayer->Sent()) and **received** (sink->Received()).
- Compute `per = 1 - (double)received / (double)offered` (guard offered>0).

### 3.7 Simulation timing
- For `packetsPerPoint=400` and `interval=2ms`, a single point needs ≈ **0.8 s** of send time; add guard for MAC timing and start/stop margins. Current 20s windows are fine.
- Keep **txPowerDbm = −20 dBm** for the SINR span as you already verified.

---

## 4) Updated run/plot commands

**Build**
```bash
cd sim && mkdir -p build && cd build
cmake -DCMAKE_PREFIX_PATH="$HOME/opt/ns3" ..
make -j$(nproc)
```

**PER vs SINR (PacketSocket path)**
```bash
./wpan-capture-sim --mode=per-sinr-sweep --transport=packet \
  --betaDb=7 --txPowerDbm=-20 --noiseFigureDb=10 --bandwidthHz=2000000 \
  --out=../../out/calibration

python3 ../../scripts/plot_recipes.py per ../../out/calibration/per_vs_sinr.csv ../../out/calibration/per_vs_sinr.png
```

**Capture test (PacketSocket path)**
```bash
./wpan-capture-sim --mode=capture-test --transport=packet \
  --captureDb=6 --betaDb=7 --txPowerDbm=-20 --noiseFigureDb=10 --bandwidthHz=2000000 \
  --out=../../out/calibration

python3 ../../scripts/plot_recipes.py cap ../../out/calibration/capture_toggle.csv ../../out/calibration/capture_toggle.png
```

---

## 5) Validation checklist (tick before closing Phase 1)

- [ ] **Counts:** At each sweep point, `offered ≥ 400`, `received ≤ offered`.
- [ ] **Knee:** `per_vs_sinr.png` shows knee ~ β (7 dB). Monotonicity holds except minor statistical noise.
- [ ] **Capture:** `capture_toggle.png` crosses near `x = 6 dB` (tolerance ±1 dB).
- [ ] **CSV schema:** matches the DoD fields exactly (distance_m/sinr_db/offered/received/per and delta_db/offered/received/per).
- [ ] **Params frozen:** `configs/capture_medium.yaml` updated for `{betaDb, captureDb, referenceLossDb, exponent}`; comments note the fit.
- [ ] **Docs updated:** README Phase 1 section reflects `--transport=packet` default.

---

## 6) Troubleshooting notes

- **Still seeing few packets?**
  - Ensure you’re on `--transport=packet` for calibration.
  - Make sure `PacketSprayerApp` `count` and `interval` are set (e.g., 400 @ 2ms).
  - Start/Stop times: give ≥ 1 s before/after send window.
- **PER curve too flat:**
  - Re‑check `txPowerDbm`; −20 dBm is known to create 2–18 dB SINR across 6–20 m.
  - Re‑tune `referenceLossDb` and `exponent` for your machine/environment seed.
- **Capture not toggling at expected Δ:**
  - Confirm interferer power is actually adjusted (SetTxDbmAndNoise on dev2).
  - Verify interferer sends concurrently to the same RX.
- **CSV shows offered=0:**
  - App start times wrong; sprayer didn’t run due to stop time too early.

---

## 7) Deliverables to commit for Phase 1
- `sim/apps/packet_sprayer.{h,cc}`
- `sim/apps/raw_l2_sink.{h,cc}`
- `sim/CMakeLists.txt` updates
- `sim/wpan_capture_sim.cc` changes (flags, L2 mode, CSV fields)
- Updated `configs/capture_medium.yaml` (final numbers)
- `out/calibration/*.csv` and `*.png` (or attach in PR)

---

## 8) Post‑Phase‑1 notes (for later phases)
- Restore MAC retries to defaults once we leave calibration.
- Keep PacketSocket path around; later phases (routing) will also use PacketSocket for app‑layer forwarding per the paper’s architecture.
- The PER/capture calibration is stable across seeds; if not, increase per‑point samples or set Nakagami m > 1 to reduce variance.

---

**End of PHASE_1.md**
