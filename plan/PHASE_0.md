# PHASE_0.md — Repo Bootstrap & Environment (Pop!\_OS / Ubuntu)

**Goal:** Stand up a minimal, reproducible ns-3 development environment using **CMake** and an **installed** ns-3 package under `~/opt/ns3`, verify a working 802.15.4 (lr-wpan) example, and capture the exact commands/configs so anyone can continue with later phases.

This phase culminates in:
- A local ns-3 install at `~/opt/ns3` discoverable by `find_package(ns3 REQUIRED)`.
- A standalone example project that builds & runs using your provided `CMakeLists.txt` and `line-lr-wpan.cc`.
- Sanity checks (pcap output, logs) and troubleshooting notes.
- Optional: Docker/devcontainer for reproducibility.

---

## 0. Prereqs (Pop!\_OS 22.04+ / Ubuntu 22.04+)

> Run all commands in a **new terminal**; use a normal user with `sudo`.

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build g++ pkg-config git \
  python3 python3-venv python3-dev \
  libgsl-dev libeigen3-dev libsqlite3-dev \
  libxml2-dev
```

> If you use Wireshark/tshark for pcap validation:
```bash
sudo apt-get install -y wireshark tshark
```

---

## 1. Fetch & build **ns-3** with CMake and install to `~/opt/ns3`

> **Why CMake & install?** Your example uses `find_package(ns3)`. Installing ns-3 exports CMake package targets (`ns3::*`) into a prefix we can point CMake to via `CMAKE_PREFIX_PATH`.

### 1.1 Clone ns-3
```bash
mkdir -p ~/src && cd ~/src
git clone https://gitlab.com/nsnam/ns-3-dev.git ns-3
cd ns-3
# (Optional) checkout a known-good tag or commit, e.g.:
# git checkout ns-3.40
```

### 1.2 Configure, build, and install
```bash
mkdir -p build && cd build
cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DNS3_BUILD_TESTS=OFF \
  -DNS3_BUILD_EXAMPLES=OFF \
  -DCMAKE_INSTALL_PREFIX=$HOME/opt/ns3 \
  ..
ninja -j"$(nproc)"
ninja install
```

### 1.3 Environment for discovery/runtime
Add this to `~/.bashrc` (and `source ~/.bashrc`):
```bash
# Let CMake find the installed ns-3 package
export CMAKE_PREFIX_PATH="$HOME/opt/ns3${CMAKE_PREFIX_PATH+:$CMAKE_PREFIX_PATH}"
# Let the runtime find ns-3 shared libs (if linked dynamically)
export LD_LIBRARY_PATH="$HOME/opt/ns3/lib${LD_LIBRARY_PATH+:$LD_LIBRARY_PATH}"
```

> **Sanity check:** You should see `ns3Config.cmake` somewhere under `~/opt/ns3/lib/cmake/ns3/`.

---

## 2. Create the standalone example project

> This mirrors your working example. Place it in a separate repo or a sibling folder.

### 2.1 Layout
```
example/
├─ CMakeLists.txt
└─ src/
   └─ line-lr-wpan.cc
```

### 2.2 `example/CMakeLists.txt` (from your working config)
```cmake
cmake_minimum_required(VERSION 3.20)
project(line_wsn LANGUAGES CXX)

# Set C++20 for compatibility with ns3 headers that use std::remove_cvref_t
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tell CMake where to find the installed ns-3 package
list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/opt/ns3")

find_package(ns3 REQUIRED)  # provides imported targets ns3::*

add_executable(line-lr-wpan src/line-lr-wpan.cc)
target_link_libraries(line-lr-wpan PRIVATE
  ns3::core
  ns3::network
  ns3::mobility
  ns3::internet
  ns3::applications
  ns3::lr-wpan
  ns3::sixlowpan
  ns3::spectrum
)
```

### 2.3 `example/src/line-lr-wpan.cc` (from your working code)
> File contents exactly as you provided (kept here verbatim for hand-off; see repo).

```cpp
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/propagation-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LineLrWpanExample");

int main (int argc, char *argv[])
{
  double simTime = 20.0;  // seconds
  double step = 10.0;     // meters between nodes
  bool enablePcap = true;

  CommandLine cmd(__FILE__);
  cmd.AddValue("simTime", "Simulation time (s)", simTime);
  cmd.AddValue("step", "Spacing between nodes (m)", step);
  cmd.Parse(argc, argv);

  // 1) Nodes
  NodeContainer nodes;
  nodes.Create(4);

  // 2) Positions (line topology)
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
  for (uint32_t i = 0; i < nodes.GetN(); ++i) { pos->Add(Vector(i * step, 0.0, 0.0)); }
  mobility.SetPositionAllocator(pos);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);

  // 3) Spectrum channel + propagation models for 802.15.4
  Ptr<SingleModelSpectrumChannel> channel = CreateObject<SingleModelSpectrumChannel>();
  Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
  channel->AddPropagationLossModel(loss);

  // FIX for Issue 1: use a supported delay model
  channel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());

  // 4) Install IEEE 802.15.4 (lr-wpan)
  LrWpanHelper wpan;
  wpan.SetChannel(channel);
  NetDeviceContainer wpanDevs = wpan.Install(nodes);

  // Assign 64-bit extended addresses and put all MACs in the same PAN
  // FIX for Issue 2: set PAN on each device's MAC; the helper's AssociateToPan() was removed.
  const uint16_t kPanId = 0x0AAA;
  for (uint32_t i = 0; i < wpanDevs.GetN(); ++i)
  {
    Ptr<ns3::lrwpan::LrWpanNetDevice> dev = DynamicCast<ns3::lrwpan::LrWpanNetDevice>(wpanDevs.Get(i));
    dev->SetAddress(Mac64Address::Allocate());                   // extended address is sufficient
    dev->GetMac()->SetPanId(kPanId);                             // same PAN for all
    // Optional: also give each node a unique short address (handy for traces)
    dev->GetMac()->SetShortAddress(Mac16Address::Allocate());
  }

  // 5) 6LoWPAN over 802.15.4
  SixLowPanHelper six;
  NetDeviceContainer sixDevs = six.Install(wpanDevs);

  // 6) Internet (IPv6)
  InternetStackHelper internet;
  internet.Install(nodes);

  Ipv6AddressHelper ipv6;
  ipv6.SetBase("2001:db8:1::", 64);
  Ipv6InterfaceContainer ifaces = ipv6.Assign(sixDevs);
  for (uint32_t i = 0; i < ifaces.GetN(); ++i)
  {
    ifaces.SetForwarding(i, true);
    ifaces.SetDefaultRouteInAllNodes(i);
  }

  // 7) UDP echo: Node3 server, Node0 client
  uint16_t port = 9;
  UdpEchoServerHelper echoServer(port);
  ApplicationContainer serverApps = echoServer.Install(nodes.Get(3));
  serverApps.Start(Seconds(1.0));
  serverApps.Stop(Seconds(simTime - 1.0));

  Ipv6Address dst = ifaces.GetAddress(3, 1); // global address
  UdpEchoClientHelper echoClient(dst, port);
  echoClient.SetAttribute("MaxPackets", UintegerValue(5));
  echoClient.SetAttribute("Interval", TimeValue(Seconds(2.0)));
  echoClient.SetAttribute("PacketSize", UintegerValue(40));
  ApplicationContainer clientApps = echoClient.Install(nodes.Get(0));
  clientApps.Start(Seconds(2.0));
  clientApps.Stop(Seconds(simTime - 2.0));

  if (enablePcap) { wpan.EnablePcapAll("line-lr-wpan", true); }

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}
```

---

## 3. Build & run the example

```bash
cd ~/src
mkdir -p example/build && cd example/build
cmake -G Ninja -DCMAKE_PREFIX_PATH="$HOME/opt/ns3" ..
ninja -j"$(nproc)"

# Run with defaults
./line-lr-wpan

# Or override args
./line-lr-wpan --simTime=30 --step=8
```

**Expected outputs**
- Binary runs without linker errors.
- `line-lr-wpan-*.pcap` files appear in the working directory (one per device) since `EnablePcapAll("line-lr-wpan", true)` is set.
- UDP echo succeeds (check logs or use FlowMonitor in later phases).

**Optional quick pcap check**
```bash
tshark -r line-lr-wpan-0-0.pcap -V | head -100
```

---

## 4. Quality gates for Phase 0

- ✅ `~/opt/ns3` contains `lib/cmake/ns3/ns3Config.cmake`.
- ✅ `CMAKE_PREFIX_PATH` and `LD_LIBRARY_PATH` are set in shell.
- ✅ `example` project configures with `find_package(ns3 REQUIRED)` and links the modules you use.
- ✅ Example runs and emits pcap files; no crashes.

---

## 5. Common pitfalls & fixes

- **CMake can’t find ns-3:**  
  Ensure `CMAKE_PREFIX_PATH` includes `~/opt/ns3`, and that you ran `ninja install` during ns-3 build.

- **Linker can’t find ns-3 at runtime:**  
  Set `LD_LIBRARY_PATH="$HOME/opt/ns3/lib:$LD_LIBRARY_PATH"` (and re-source your shell).

- **C++ standard mismatch:**  
  Use `set(CMAKE_CXX_STANDARD 20)` as in the example; some ns-3 headers (type traits) rely on C++20.

- **lr-wpan PAN association API differences:**  
  The helper’s `AssociateToPan()` has changed over time; set PAN IDs directly on each MAC as in the example loop.

- **Propagation delay model required:**  
  Always set a valid delay model (e.g., `ConstantSpeedPropagationDelayModel`) on `SpectrumChannel`.

---

## 6. Optional: Devcontainer / Docker

> Useful for pinned builds in CI and to onboard teammates quickly.

### 6.1 Devcontainer sketch (VS Code)
`.devcontainer/devcontainer.json`
```json
{
  "name": "ns3-cmake",
  "build": { "dockerfile": "Dockerfile" },
  "settings": { "terminal.integrated.defaultProfile.linux": "bash" },
  "remoteUser": "vscode",
  "postCreateCommand": "bash -lc 'echo done'"
}
```

### 6.2 Dockerfile sketch
```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
  build-essential cmake ninja-build g++ pkg-config git \
  python3 python3-venv python3-dev libgsl-dev libeigen3-dev libsqlite3-dev libxml2-dev \
  && rm -rf /var/lib/apt/lists/*

# Build & install ns-3 into /opt/ns3 (repeat steps from Phase 1 here)
# ...

ENV CMAKE_PREFIX_PATH=/opt/ns3
ENV LD_LIBRARY_PATH=/opt/ns3/lib
```

---

## 7. Hand-off checklist (Definition of Done)

- [ ] `PHASE_0.md` and `README.md` committed with the exact commands above.
- [ ] ns-3 built and installed at `~/opt/ns3` (or `/opt/ns3` in container).
- [ ] Example project builds via `cmake .. && ninja` and runs successfully.
- [ ] pcap files present; `tshark` (optional) can inspect them.
- [ ] Troubleshooting section validated on a clean shell.

---

## 8. Next step pointers

- Proceed to **Phase 1** (PHY calibration). Keep the example project; in later phases we’ll migrate code into `sim/` (controllers, helpers) and add metrics exporters while preserving the same CMake pattern and `find_package(ns3)`.
