# PHASE_1.md — PHY Bring‑Up & PER vs SINR Calibration (Capture Threshold)

**Goal:** Establish a calibrated 802.15.4 PHY on `SpectrumChannel` such that **PER vs SINR** exhibits the expected threshold behavior and the **capture margin** (x dB) matches the configured **β** (SINR threshold). This phase produces a repeatable calibration harness, CSVs, and plots that lock down `configs/capture_medium.yaml` for later phases.

This document is derived from **PHASES.md · Phase 1** and aligned with **PLAN.md**. It assumes you completed **PHASE_0.md** (ns‑3 installed at `~/opt/ns3`, example builds).

---

## 1) Outcomes (Artifacts & Definition of Done)

**Artifacts**

- `sim/wpan_capture_sim.cc` — a small harness with two modes: `per-sinr-sweep` and `capture-test`.
- `sim/helpers/channel_export.h/.cc` — (stub) utilities to compute link budgets / SINR from configured models (LogDistance, noise figure).
- `configs/capture_medium.yaml` — finalized PHY params: `betaDb`, `captureDb`, `noiseFigureDb`, `bandwidthHz`, propagation/fading knobs.
- Calibration data under `out/calibration/`:
  - `per_vs_sinr.csv` + `per_vs_sinr.png`
  - `capture_toggle.csv` + `capture_toggle.png`

**Definition of Done (DoD)**

- The **PER(SINR)** curve shows a knee near the chosen `betaDb` with a monotonic decrease in PER as SINR rises.
- The **3‑node capture test** toggles success probability around `captureDb = x` dB differential: desired wins when ≥ x dB louder; loses when ≤ x dB quieter (within tolerance).
- `configs/capture_medium.yaml` is committed and referenced by Phase 2+.

---

## 2) Build Integration

Two equivalent layouts are supported. Pick one and keep it for the rest of the repo.

### Option A — Standalone CMake target (recommended)

```
ns3-bcp-ctp-capture/
├─ sim/
│  ├─ CMakeLists.txt
│  ├─ wpan_capture_sim.cc
│  └─ helpers/
│     ├─ channel_export.h
│     └─ channel_export.cc
├─ configs/
│  └─ capture_medium.yaml
└─ ...
```

**sim/CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(wpan_capture_sim LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure the ns-3 install is discoverable (see PHASE_0.md)
list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/opt/ns3")
find_package(ns3 REQUIRED)

add_executable(wpan-capture-sim wpan_capture_sim.cc helpers/channel_export.cc)
target_include_directories(wpan-capture-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wpan-capture-sim PRIVATE
  ns3::core ns3::network ns3::mobility ns3::internet ns3::applications
  ns3::lr-wpan ns3::sixlowpan ns3::spectrum ns3::propagation
)
```

**Build & run**

```bash
cd sim && mkdir -p build && cd build
cmake -G Ninja -DCMAKE_PREFIX_PATH="$HOME/opt/ns3" ..
ninja -j"$(nproc)"
./wpan-capture-sim --mode=per-sinr-sweep --out=../../out/calibration
```

### Option B — ns‑3 tree (`scratch/`)

Drop `wpan_capture_sim.cc` under `ns-3/scratch/` and run `./ns3 run "scratch/wpan_capture_sim --mode=per-sinr-sweep ..."`. Keep the code identical; only the build system differs.

---

## 3) Configuration (capture_medium.yaml)

Create **configs/capture_medium.yaml** and evolve values during calibration until DoD is met.

```yaml
# configs/capture_medium.yaml
phy:
  betaDb: 7.0            # SINR threshold where PER curve “knees”
  captureDb: 6.0         # desired must exceed interferer by >= x dB
  txPowerDbm: 0.0        # default Tx power
  noiseFigureDb: 10.0
  bandwidthHz: 2000000   # ~2 MHz (IEEE 802.15.4 OQPSK 2.4 GHz)
  centerFreqHz: 2405000000  # Channel 11 (example)
propagation:
  model: LogDistance
  exponent: 3.0
  referenceDistance: 1.0      # m
  referenceLossDb: 40.0       # path loss at 1 m (tune during fit)
fading:
  model: Nakagami
  m: 1.0                      # =1 Rayleigh; increase to reduce variance
mac:
  ackEnabled: true
  maxFrameRetries: 3
sweep:
  distances_m: [6, 8, 10, 12, 14, 16, 18, 20]  # for per-sinr sweep
  packetsPerPoint: 400
outDir: out/calibration
```

> **Notes**  
> • `referenceLossDb` is a convenient knob to align received power to realistic values for 802.15.4 (−85…−100 dBm thresholds).  
> • If you calibrate at sub‑GHz, update `centerFreqHz` and `bandwidthHz` (e.g., 200 kHz).

---

## 4) Harness Code

Below is a compact, well‑commented skeleton you can paste into `sim/wpan_capture_sim.cc`. It wires up 802.15.4 on a `SingleModelSpectrumChannel`, exposes two **modes**, and logs CSV rows per experiment point.

> This is a minimal “Phase 1” harness. Later phases will migrate logic to apps/controllers; for now we keep it simple.

```cpp
// sim/wpan_capture_sim.cc
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/propagation-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include <fstream>
#include <iomanip>

using namespace ns3;
using std::string;

NS_LOG_COMPONENT_DEFINE("WpanCaptureCalib");

struct CalibCfg {
  double betaDb = 7.0;
  double captureDb = 6.0;
  double txPowerDbm = 0.0;
  double noiseFigureDb = 10.0;
  double bandwidthHz = 2e6;
  double centerFreqHz = 2.405e9;
  // sweep
  std::vector<double> distances;
  uint32_t packetsPerPoint = 400;
  string outDir = "out/calibration";
};

static double
DbmToW(double dbm) { return std::pow(10.0, (dbm - 30.0)/10.0); }

static double
WToDb(double w) { return 10.0 * std::log10(w); }

static double
ThermalNoiseDbm(double bandwidthHz, double noiseFigureDb) {
  // kTB (dBm) = -174 dBm/Hz + 10log10(B) + NF
  return -174.0 + 10.0*std::log10(bandwidthHz) + noiseFigureDb;
}

// Hook MAC traces for per-link success (TxOk) vs failures (TxDrop)
struct MacCounters {
  uint32_t ok{0}, drop{0};
};

static void
OnMacTxOk(MacCounters* c, Ptr<const Packet> p) { c->ok++; }

static void
OnMacTxDrop(MacCounters* c, Ptr<const Packet> p) { c->drop++; }

// Build a 2-node or 3-node scene on a Spectrum channel
struct WpanScene {
  NodeContainer nodes;
  NetDeviceContainer devs;
  Ptr<SingleModelSpectrumChannel> channel;
  Ptr<LogDistancePropagationLossModel> loss;
};

static WpanScene
MakeScene(uint32_t N, const CalibCfg& cfg) {
  WpanScene s;
  s.nodes.Create(N);

  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(s.nodes);

  s.channel = CreateObject<SingleModelSpectrumChannel>();
  s.loss = CreateObject<LogDistancePropagationLossModel>();
  s.loss->SetReference( cfg.referenceDistance?cfg.referenceDistance:1.0,
                        cfg.referenceLossDb?cfg.referenceLossDb:40.0,
                        cfg.exponent?cfg.exponent:3.0);
  s.channel->AddPropagationLossModel(s.loss);
  s.channel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());

  LrWpanHelper wpan;
  wpan.SetChannel(s.channel);
  s.devs = wpan.Install(s.nodes);

  const uint16_t kPanId = 0x0AAA;
  for (uint32_t i = 0; i < s.devs.GetN(); ++i) {
    auto dev = DynamicCast<lrwpan::LrWpanNetDevice>(s.devs.Get(i));
    dev->SetAddress(Mac64Address::Allocate());
    dev->GetMac()->SetPanId(kPanId);
    dev->GetMac()->SetShortAddress(Mac16Address::Allocate());
    // Tx power (dBm) as attribute on the PHY
    dev->GetPhy()->SetTransmitPower(cfg.txPowerDbm);
  }

  // IPv6 stack for simple UDP echo
  InternetStackHelper internet; internet.Install(s.nodes);
  SixLowPanHelper six; auto sixDevs = six.Install(s.devs);
  Ipv6AddressHelper ipv6; ipv6.SetBase("2001:db8:calib::", 64);
  auto ifaces = ipv6.Assign(sixDevs);
  for (uint32_t i=0;i<ifaces.GetN();++i){ ifaces.SetForwarding(i,true); ifaces.SetDefaultRouteInAllNodes(i); }

  return s;
}

// Compute mean SINR (dB) from LogDistance model for desired tx->rx vs an interferer (optional)
static double
ComputeSinrDb(Ptr<LogDistancePropagationLossModel> loss, Ptr<MobilityModel> tx, Ptr<MobilityModel> rx,
              const std::vector<Ptr<MobilityModel>>& interferers, double txPowerDbm, double bandwidthHz, double nfDb)
{
  double prDesiredDbm = loss->CalcRxPower(txPowerDbm, tx, rx); // dBm
  double signalW = DbmToW(prDesiredDbm);

  double noiseDbm = ThermalNoiseDbm(bandwidthHz, nfDb);
  double noiseW = DbmToW(noiseDbm);

  double interfW = 0.0;
  for (auto& itx : interferers) {
    double prDbm = loss->CalcRxPower(txPowerDbm, itx, rx);
    interfW += DbmToW(prDbm);
  }
  double sinr = signalW / (noiseW + interfW);
  return WToDb(sinr);
}

int main(int argc, char** argv) {
  std::string mode = "per-sinr-sweep";
  CalibCfg cfg;

  // Additional propagation knobs (provided via CommandLine to set LogDistance params)
  double refLossDb = 40.0, expn = 3.0, refDist = 1.0;

  CommandLine cmd;
  cmd.AddValue("mode", "per-sinr-sweep | capture-test", mode);
  cmd.AddValue("betaDb", "SINR threshold beta (dB)", cfg.betaDb);
  cmd.AddValue("captureDb", "capture margin x (dB)", cfg.captureDb);
  cmd.AddValue("txPowerDbm", "Tx power (dBm)", cfg.txPowerDbm);
  cmd.AddValue("noiseFigureDb", "Noise figure (dB)", cfg.noiseFigureDb);
  cmd.AddValue("bandwidthHz", "Receiver bandwidth (Hz)", cfg.bandwidthHz);
  cmd.AddValue("out", "Output directory", cfg.outDir);
  cmd.AddValue("refLossDb", "LogDistance reference loss at 1m (dB)", refLossDb);
  cmd.AddValue("exponent", "LogDistance exponent", expn);
  cmd.AddValue("refDist", "LogDistance reference distance (m)", refDist);
  cmd.Parse(argc, argv);

  // Embed sweep distances if not provided via YAML (Phase 1 keeps it simple)
  if (cfg.distances.empty()) {
    cfg.distances = {6,8,10,12,14,16,18,20};
  }

  // Create output dir
  if (cfg.outDir.size()) {
    std::string mkdir = "mkdir -p " + cfg.outDir;
    system(mkdir.c_str());
  }

  if (mode == "per-sinr-sweep") {
    std::ofstream csv(cfg.outDir + "/per_vs_sinr.csv");
    csv << "distance_m,sinr_db,packets,tx_ok,tx_drop,per\n";

    for (double d : cfg.distances) {
      auto scene = MakeScene(2, cfg);
      // Positions: node0 @ (0,0,0), node1 @ (d,0,0)
      scene.nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0,0,0));
      scene.nodes.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(d,0,0));

      // UDP echo (0 -> 1)
      uint16_t port = 9;
      UdpEchoServerHelper server(port);
      auto appsS = server.Install(scene.nodes.Get(1));
      appsS.Start(Seconds(0.5)); appsS.Stop(Seconds(10.0));

      UdpEchoClientHelper client(Ipv6Address("2001:db8:calib::2"), port);
      client.SetAttribute("MaxPackets", UintegerValue(cfg.packetsPerPoint));
      client.SetAttribute("Interval", TimeValue(MilliSeconds(10)));
      client.SetAttribute("PacketSize", UintegerValue(40));
      auto appsC = client.Install(scene.nodes.Get(0));
      appsC.Start(Seconds(1.0)); appsC.Stop(Seconds(10.0));

      // Trace MAC on sender (best proxy for link-level PER in this setup)
      MacCounters ctr{};
      auto dev0 = DynamicCast<lrwpan::LrWpanNetDevice>(scene.devs.Get(0));
      auto mac0 = dev0->GetMac();
      mac0->TraceConnectWithoutContext("MacTxOk", MakeBoundCallback(&OnMacTxOk, &ctr));
      mac0->TraceConnectWithoutContext("MacTxDrop", MakeBoundCallback(&OnMacTxDrop, &ctr));

      // Compute mean SINR (no interferers)
      auto mm0 = scene.nodes.Get(0)->GetObject<MobilityModel>();
      auto mm1 = scene.nodes.Get(1)->GetObject<MobilityModel>();
      double sinrDb = ComputeSinrDb(scene.loss, mm0, mm1, {}, cfg.txPowerDbm, cfg.bandwidthHz, cfg.noiseFigureDb);

      Simulator::Stop(Seconds(12.0));
      Simulator::Run();
      Simulator::Destroy();

      double per = 0.0;
      uint32_t n = ctr.ok + ctr.drop;
      if (n > 0) per = (double)ctr.drop / (double)n;
      csv << std::fixed << std::setprecision(2)
          << d << "," << sinrDb << "," << n << "," << ctr.ok << "," << ctr.drop << "," << per << "\n";
    }
    csv.close();
  }
  else if (mode == "capture-test") {
    // Three nodes: 0->1 (desired), 2 interferer near 1. Sweep P2-P0 around captureDb.
    std::ofstream csv(cfg.outDir + "/capture_toggle.csv");
    csv << "delta_db,desired_ok,desired_drop,per\n";

    std::vector<double> deltas = {-8,-6,-4,-2,0,2,4,6,8};
    for (double ddb : deltas) {
      auto scene = MakeScene(3, cfg);
      scene.nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0,0,0));   // desired TX
      scene.nodes.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(8,0,0));   // RX
      scene.nodes.Get(2)->GetObject<MobilityModel>()->SetPosition(Vector(8,1.0,0)); // interferer near RX

      // Adjust powers: P0 = base, P2 = base + ddb
      auto dev0 = DynamicCast<lrwpan::LrWpanNetDevice>(scene.devs.Get(0));
      auto dev2 = DynamicCast<lrwpan::LrWpanNetDevice>(scene.devs.Get(2));
      dev0->GetPhy()->SetTransmitPower(cfg.txPowerDbm);
      dev2->GetPhy()->SetTransmitPower(cfg.txPowerDbm + ddb);

      // Start desired UDP flow 0->1 (echo)
      uint16_t port = 9;
      UdpEchoServerHelper server(port);
      auto appsS = server.Install(scene.nodes.Get(1));
      appsS.Start(Seconds(0.5)); appsS.Stop(Seconds(12.0));

      UdpEchoClientHelper client(Ipv6Address("2001:db8:calib::2"), port);
      client.SetAttribute("MaxPackets", UintegerValue(cfg.packetsPerPoint));
      client.SetAttribute("Interval", TimeValue(MilliSeconds(10)));
      client.SetAttribute("PacketSize", UintegerValue(40));
      auto appsC = client.Install(scene.nodes.Get(0));
      appsC.Start(Seconds(1.0)); appsC.Stop(Seconds(12.0));

      // Interferer 2 sends CBR to RX 1 concurrently
      OnOffHelper interferer("ns3::UdpSocketFactory", Address(Inet6SocketAddress(Ipv6Address("2001:db8:calib::2"), port)));
      interferer.SetConstantRate(DataRate("100kbps"), 40);
      auto appsI = interferer.Install(scene.nodes.Get(2));
      appsI.Start(Seconds(1.0)); appsI.Stop(Seconds(12.0));

      MacCounters ctr{};
      auto mac0 = dev0->GetMac();
      mac0->TraceConnectWithoutContext("MacTxOk", MakeBoundCallback(&OnMacTxOk, &ctr));
      mac0->TraceConnectWithoutContext("MacTxDrop", MakeBoundCallback(&OnMacTxDrop, &ctr));

      Simulator::Stop(Seconds(13.0));
      Simulator::Run();
      Simulator::Destroy();

      uint32_t n = ctr.ok + ctr.drop;
      double per = (n>0) ? (double)ctr.drop / (double)n : 1.0;
      csv << ddb << "," << ctr.ok << "," << ctr.drop << "," << per << "\n";
    }
    csv.close();
  }
  return 0;
}
```

> **Trace names:** The `LrWpanMac` trace sources `MacTxOk` and `MacTxDrop` are present in recent ns‑3 (names may differ slightly across commits). If they differ on your version, list trace sources via source to confirm and adjust (`grep "TraceSource" -n src/lr-wpan/*`).

---

## 5) Helper: channel_export (stub)

A tiny helper holds the link‑budget math so both the harness and later phases can reuse it. Minimal stub shown; feel free to expand in Phase 8 (topology export).

**sim/helpers/channel_export.h**

```cpp
#pragma once
#include "ns3/propagation-module.h"
#include "ns3/mobility-model.h"

namespace calib {
double DbmToW(double dbm);
double ThermalNoiseDbm(double bandwidthHz, double noiseFigureDb);
double CalcMeanRxDbm(ns3::Ptr<ns3::LogDistancePropagationLossModel> loss,
                     double txPowerDbm,
                     ns3::Ptr<ns3::MobilityModel> tx,
                     ns3::Ptr<ns3::MobilityModel> rx);
double CalcPairSinrDb(ns3::Ptr<ns3::LogDistancePropagationLossModel> loss,
                      double txPowerDbm,
                      ns3::Ptr<ns3::MobilityModel> desiredTx,
                      ns3::Ptr<ns3::MobilityModel> rx,
                      ns3::Ptr<ns3::MobilityModel> interferer,
                      double bandwidthHz, double noiseFigureDb);
}
```

**sim/helpers/channel_export.cc**

```cpp
#include "channel_export.h"
#include <cmath>

namespace calib {
double DbmToW(double dbm){ return std::pow(10.0, (dbm - 30.0)/10.0); }
static double WToDb(double w){ return 10.0*std::log10(w); }
double ThermalNoiseDbm(double B, double NF){ return -174.0 + 10.0*std::log10(B) + NF; }
double CalcMeanRxDbm(ns3::Ptr<ns3::LogDistancePropagationLossModel> loss,
                     double txPowerDbm,
                     ns3::Ptr<ns3::MobilityModel> tx,
                     ns3::Ptr<ns3::MobilityModel> rx) {
  return loss->CalcRxPower(txPowerDbm, tx, rx);
}
double CalcPairSinrDb(ns3::Ptr<ns3::LogDistancePropagationLossModel> loss,
                      double txPowerDbm,
                      ns3::Ptr<ns3::MobilityModel> desiredTx,
                      ns3::Ptr<ns3::MobilityModel> rx,
                      ns3::Ptr<ns3::MobilityModel> interferer,
                      double B, double NF) {
  double sDbm = loss->CalcRxPower(txPowerDbm, desiredTx, rx);
  double iDbm = loss->CalcRxPower(txPowerDbm, interferer, rx);
  double nDbm = ThermalNoiseDbm(B, NF);
  double sW = DbmToW(sDbm), iW = DbmToW(iDbm), nW = DbmToW(nDbm);
  return WToDb(sW / (iW + nW));
}
}
```

---

## 6) Running the Experiments

### 6.1 PER vs SINR sweep

```bash
# build
cd sim/build && ninja

# run sweep
./wpan-capture-sim \
  --mode=per-sinr-sweep \
  --betaDb=7 --txPowerDbm=0 --noiseFigureDb=10 --bandwidthHz=2000000 \
  --out=../../out/calibration

# output
# out/calibration/per_vs_sinr.csv
#   distance_m,sinr_db,packets,tx_ok,tx_drop,per
#   6.00,  15.20, 400, 398, 2, 0.005
#   8.00,  12.10, 400, 392, 8, 0.020
#   ...
```

### 6.2 Capture test (3‑node)

```bash
./wpan-capture-sim \
  --mode=capture-test \
  --captureDb=6 --txPowerDbm=0 --noiseFigureDb=10 --bandwidthHz=2000000 \
  --out=../../out/calibration

# out/calibration/capture_toggle.csv
# delta_db,desired_ok,desired_drop,per
# -8,  41,359,0.897
# -6,  89,311,0.777
# -4, 180,220,0.550
# -2, 275,125,0.313
#  0, 330, 70,0.175
#  2, 360, 40,0.100
#  4, 382, 18,0.047
#  6, 392,  8,0.020
#  8, 398,  2,0.005
```

> Expect the cross‑over near `delta_db ≈ captureDb` (±1–2 dB depending on retries/fading).

---

## 7) Plotting (scripts/plot_recipes.py excerpts)

Create (or extend) `scripts/plot_recipes.py` with two helpers. This file is also used in later phases.

```python
# scripts/plot_recipes.py (snippets)
import csv, math
import matplotlib.pyplot as plt

def plot_per_vs_sinr(csv_path, out_path):
    xs, ys = [], []
    with open(csv_path) as f:
        r = csv.DictReader(f)
        for row in r:
            xs.append(float(row["sinr_db"]))
            ys.append(float(row["per"]))
    xs2, ys2 = zip(*sorted(zip(xs, ys)))
    plt.figure()
    plt.semilogy(xs2, ys2, marker="o")
    plt.xlabel("SINR (dB)"); plt.ylabel("PER (log scale)")
    plt.grid(True, which="both", ls=":")
    plt.savefig(out_path, bbox_inches="tight")

def plot_capture_toggle(csv_path, out_path):
    xs, ys = [], []
    with open(csv_path) as f:
        r = csv.DictReader(f)
        for row in r:
            xs.append(float(row["delta_db"]))
            ys.append(float(row["per"]))
    xs2, ys2 = zip(*sorted(zip(xs, ys)))
    plt.figure()
    plt.plot(xs2, ys2, marker="o")
    plt.xlabel("Interferer–Desired Δ (dB)")
    plt.ylabel("PER of desired link")
    plt.grid(True, ls=":")
    plt.savefig(out_path, bbox_inches="tight")
```

**Run**

```bash
python3 scripts/plot_recipes.py per --in out/calibration/per_vs_sinr.csv --out out/calibration/per_vs_sinr.png
python3 scripts/plot_recipes.py cap --in out/calibration/capture_toggle.csv --out out/calibration/capture_toggle.png
```

> Implementation hint: dispatch on `sys.argv[1] in {"per","cap"}` and call the corresponding function.

---

## 8) Validation Checklist (sign‑off)

1. **PER(SINR) Knee:** The semilog PER curve’s knee occurs near `betaDb` (± ~1 dB after fitting `referenceLossDb` and `exponent`).  
2. **Monotonicity:** PER strictly decreases as SINR increases across sweep points (allowing small statistical noise).  
3. **Capture Margin:** In `capture-test`, PER drops rapidly once `delta_db ≥ captureDb`; for `delta_db ≤ -captureDb`, PER is high (≫0.5).  
4. **Sensitivity to Fading:** Increasing `fading.m` reduces variance between seeds; choose whether you want a “clean” curve (`m≥3`) or realistic Rayleigh (`m=1`).  
5. **Stability Across Seeds:** Re‑run with 3 seeds; curves align within confidence bands.

If (1)–(5) hold, **freeze** `configs/capture_medium.yaml` and mark Phase 1 complete.

---

## 9) Troubleshooting

- **PER curve too flat / knee misplaced**  
  • Adjust `propagation.referenceLossDb` and `exponent` to move the received power regime.  
  • Verify `bandwidthHz` and `noiseFigureDb` (thermal noise level).  
  • Ensure **ACKs** are enabled and `maxFrameRetries` is modest (high retries smooth the knee).

- **Trace names not found**  
  • Run `grep -n "TraceSource" src/lr-wpan/*` in your ns‑3 tree to confirm exact names for your commit; update the `TraceConnectWithoutContext` strings accordingly.

- **No difference in capture test across ΔdB**  
  • Confirm the interferer addresses the **same receiver** as the desired link.  
  • Increase interferer rate (e.g., `OnOff` data rate) to cause persistent overlap.  
  • Reduce `maxFrameRetries` to expose instantaneous collisions.

- **Link budget inconsistent with logs**  
  • Remember: `CalcRxPower` returns **mean** large‑scale power; the PHY’s error model still adds stochasticity. That’s OK for Phase 1; later phases rely on averages for LPF export.

---

## 10) Commit Plan

```
git add sim/wpan_capture_sim.cc sim/helpers/channel_export.* configs/capture_medium.yaml
git commit -m "Phase 1: PHY calibration harness, PER vs SINR & capture tests"
```

---

## 11) Appendix — CSV Schemas

**per_vs_sinr.csv**

```
distance_m: double   # meters between Tx and Rx
sinr_db:    double   # computed mean SINR for this geometry
packets:    uint32   # attempts by sender MAC (ok+drop)
tx_ok:      uint32
tx_drop:    uint32
per:        double   # tx_drop / (tx_ok + tx_drop)
```

**capture_toggle.csv**

```
delta_db:       double   # P_interferer - P_desired (dB)
desired_ok:     uint32
desired_drop:   uint32
per:            double
```

---

## 12) What Carries Forward to Later Phases

- `configs/capture_medium.yaml` is now the single source of truth for PHY parameters (β, x, NF, bandwidth, propagation).  
- `channel_export.*` will be extended in **Phase 8** to dump topology and gain matrices for the LPF pipeline.  
- The harness will evolve into `wpan_capture_sim.cc` with controllers and metrics logging (Phases 3–7) but retains the same CMake target and ns‑3 linkage.

**End of PHASE_1.md**
