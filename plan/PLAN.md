# ns-3 6-Node Ring Topology Plan (Step-by-Step, Minimal & Testable)

This plan starts from **ns-3-dev** lr-wpan examples and builds, step by step, to a 6-node ring scenario that can toggle a “bad arc” by *software blocking* a directed link on a timer (no PHY interference required initially). Each milestone compiles and has a clear expected behavior so you can validate progress.

---

## Overview of Milestones

- **Milestone 0 — Build & sanity-check ns-3**
- **Milestone 1 — Two nodes talking (baseline)**
- **Milestone 2 — 6-node ring (neighbor pings)**
- **Milestone 3 — Minimal multi-hop “ring forwarder” (clockwise only)**
- **Milestone 4 — Controllers: Snapshot-Global vs Local (skeleton)**
- **Milestone 5 — Toggle “bad arc” by software blocking a directed link**
- **Milestone 6 — Add snapshot epochs `T_info` to vary ρ**
- **Milestone 7 — Finite buffers `B` + waste/drop counters**

At the end, you’ll have a deterministic demo of the paper’s phenomenon (Local outperforms Snapshot-Global as ρ grows), ready to extend with ETX gating or interference models.

---

## Milestone 0 — Build & sanity-check ns-3

```bash
git clone https://gitlab.com/nsnam/ns-3-dev.git
cd ns-3-dev
./waf configure --enable-examples --enable-tests
./waf build
# (or the newer wrapper)
# ./ns3 configure --enable-examples --enable-tests && ./ns3 build
```

Run an lr-wpan example to confirm the stack:

```bash
./waf --run lr-wpan-data
```

**Expected:** prints MCPS-DATA.indication / PHY transitions.

---

## Milestone 1 — Copy example into `scratch/` and run

```bash
cp src/lr-wpan/examples/lr-wpan-data.cc scratch/ring6-step1.cc
./waf --run "scratch/ring6-step1"
```

**Expected:** Same behavior as the example (establishes working build + callbacks).

---

## Milestone 2 — 6-node ring (neighbor unicast)

Create `scratch/ring6-step2.cc` (copy from step1 and edit):

1. Create 6 nodes; install `LrWpanNetDevice` on each.
2. Attach PHYs to a single `SingleModelSpectrumChannel` with `LogDistancePropagationLossModel` + `ConstantSpeedPropagationDelayModel`.
3. Place nodes on a circle via `ConstantPositionMobilityModel` (radius ~10–20 m).
4. Set 16-bit MAC addresses you control (`Mac16Address("00:01")` … `"00:06"`).
5. Test neighbor unicast both directions for each ring edge.

Build & run:

```bash
cp scratch/ring6-step1.cc scratch/ring6-step2.cc
# edit per above
./waf --run "scratch/ring6-step2"
```

**Expected:** 12 successful single-hop deliveries (each arc, both directions).

---

## Milestone 3 — Minimal multi-hop “ring forwarder” (clockwise only)

Create `scratch/ring6-step3.cc`:

- Add a tiny `RingHeader` (dstId, srcId, ttl) or just one-byte payload tag.
- Register MAC receive callback (`SetMcpsDataIndicationCallback`):
  - If `dstId == myId` and `myId == sink(=0)`: log DELIVERED.
  - Else: **forward clockwise** to `(myId + 1) % 6`.
- Source: node 3 emits packets (e.g., 5 pps) destined to sink 0.

**Expected:** Packets traverse 5 hops; you see per-hop indications in order.

---

## Milestone 4 — Controllers (Snapshot-Global vs Local) as policy flags

- **Snapshot-Global (static parents):** at t=0 install `parent[i]` toward sink in clockwise order (e.g., `parent[1]=0`, `parent[2]=1`, … `parent[0]=0`). Forwarding always uses `parent[i]` until next snapshot.
- **Local (myopic):** per TX, pick next hop among `{cw, ccw}`:
  - If exactly one is available → use it.
  - If both available → default to clockwise (will refine later).

Add CLI flag: `--mode=global|local`.

**Expected:** Before any “bad arc,” both modes deliver similarly.

---

## Milestone 5 — Toggle a “bad arc” by *software blocking* a directed link

Implement a link **blocklist** to refuse sends from i→j during a window (no PHY changes).

```cpp
// global or singleton
std::set<std::pair<uint16_t,uint16_t>> blocked;

bool TrySend(uint16_t from, uint16_t to, Ptr<Packet> p) {
  if (blocked.count({from,to})) {
    // emulate ETX=∞ on this directed edge
    return false; // caller may re-enqueue or drop
  }
  return DoMcpsSend(from, to, p); // identical to lr-wpan-data.cc pattern
}

// schedule the fault window
Simulator::Schedule(Seconds(20), []{ blocked.insert({2,3}); });
Simulator::Schedule(Seconds(60), []{ blocked.erase({2,3});  });
```

**Expected:**  
- **Global:** when `(2→3)` is blocked, traffic builds up upstream of node 2; goodput dips until 60 s, then recovers.  
- **Local:** nodes route the *other way around the ring*; goodput remains high and queues bounded.

---

## Milestone 6 — Add snapshot epochs `T_info` (vary ρ = `T_info/T_dyn`)

In Global mode, re-install parents only every `T_info` seconds:

```cpp
void InstallParentsFromSnapshot () {
  for (uint16_t i=1; i<6; ++i) parent[i] = (i+5) % 6; // toward sink 0
}

void StartSnapshots (double Tinfo) {
  InstallParentsFromSnapshot ();
  Simulator::Schedule (Seconds (Tinfo), &StartSnapshots, Tinfo);
}
```

Set the bad-arc ON/OFF to define `T_dyn` (e.g., ON from 20–60 s → `T_dyn = 40 s`). Sweep `--Tinfo ∈ {10,30,60,120}` to vary ρ.

**Expected:** Larger ρ → Snapshot-Global remains committed to the bad arc longer → more drops/waste, lower goodput.

_Implementation note:_ `InstallSnapshotParents` keeps clockwise parents by default but walks the entire path to the sink; if any hop in that direction is blocked at the current snapshot time, the node (and thus its upstream traffic) switches to the counter-clockwise neighbor for that epoch.

---

## Milestone 7 — Finite buffers `B`, packet IDs, waste & drops

- Add an app-level TX queue (cap `B`) in front of `TrySend`; on enqueue when full → **admission drop**.
- Tag each packet with a 64-bit ID.
- Log events:
  - `TX(from,to,ID)` per hop,
  - `DELIVERED(ID)` at sink,
  - `DROP(ID,node)` on overflow or TTL expire.
- Compute:
- **Waste** = count of `TX` for IDs that never deliver and later drop downstream.
- **Drops** = all `DROP` events.

**Expected:** In Global mode, drops/waste spike during the bad-arc window; Local remains low.

_Implementation note:_ Each node now owns a bounded software queue (configurable via `--B`) that must accept packets before they reach `McpsDataRequest`. The run logs `TX/DROP/DELIVERED` events per sequence ID and tracks `queueDrops` (admission failures) plus `wasteTx` (all transmissions spent on packets that eventually drop). These new counters flow through the `RESULT` line, sweep CSV, and Matplotlib plots.

---

## Command-line flags to add (quality-of-life)

```
--mode=global|local
--badArc=2,3
--badOn=20 --badOff=60
--Tinfo=60
--B=20
--rate=5pps
--simTime=180
--seed=1
```

---

## Notes & Next Steps

- This “software blocking” is the simplest deterministic way to emulate a very bad ETX. Later, you can swap it for:
  - a custom `PropagationLossModel` returning ∞ on (i→j) during `[t_on, t_off)`,
  - or a receiver-side filter that ignores frames from i during the window.
- To move toward the paper’s **DPP/BCP gating**, compute ETX with an EWMA over data/ACK and use a threshold like: transmit if `Q_i - Q_j > V * ETX_ij`.

---

## Where in the tree to look (for patterns & callbacks)

- Examples to copy from: `src/lr-wpan/examples/lr-wpan-data.cc`, `lr-wpan-beacon-mode.cc`
- Tests that show channel wiring & mobility: `src/lr-wpan/test/lr-wpan-collision-test.cc`, `lr-wpan-cca-test.cc`
- Module docs (service primitives): `src/lr-wpan/doc/lr-wpan.rst` (and Doxygen pages)

---

## Validation Checklist per Milestone

- **M0 (Done):** ns-3-dev configured/built ahead of this plan (baseline `./ns3 build`).
- **M1 (Done):** `scratch/ring6-step1.cc` cloned from `lr-wpan-data` and verified via  
  Tests: `./ns3 build scratch/ring6-step1` and `./build/scratch/ns3.46.1-ring6-step1-default` (short + `--extended=1`).
- **M2 (Done):** `scratch/ring6-step2.cc` builds the 6-node ring; logs show 12 neighbor deliveries.  
  Tests: `./ns3 build scratch/ring6-step2`, `./build/scratch/ns3.46.1-ring6-step2-default`.
- **M3 (Done):** `scratch/ring6-step3.cc` adds the clockwise forwarder and CLI knobs; packets traverse 3→0 through five hops.  
  Tests: `./ns3 build scratch/ring6-step3`, `./build/scratch/ns3.46.1-ring6-step3-default`.
- **M4 (Done):** `scratch/ring6-step4.cc` introduces Global vs Local controllers plus metrics/sweep tooling.  
  Tests: `./ns3 build scratch/ring6-step4`, `./build/scratch/ns3.46.1-ring6-step4-default --mode=global`, `--mode=local`, and automation via  
  `./ring6_sweep.py --ns3-dir references/ns-3-dev --log-dir sweeps --tinfo 0 4 --modes global local --bad-arc 2,3 --bad-on 2 --bad-off 8 --count 3 --rate 2`.
- **M5 (Done):** Directed fault windows (`--badArc/badOn/badOff`) plus RESULT counters differentiate behaviors (global blocked vs local reroute).  
  Tests: `./build/scratch/ns3.46.1-ring6-step4-default --mode=global --badArc=4,5 --badOn=2 --badOff=8 --count=6 --rate=1` and  
  `./build/scratch/ns3.46.1-ring6-step4-default --mode=local --badArc=4,5 --badOn=2 --badOff=8 --count=6 --rate=1`.  
  Visualization: `python3 plot_sweep.py --csv sweeps/ring6_sweep_results.csv --out-dir sweeps/plots`.
- **M6 (Done):** Enhanced `InstallSnapshotParents` so snapshots prefer clockwise routes but fall back to counter-clockwise when the current window blocks any hop to the sink. Swept `Tinfo ∈ {0, 0.5, 1, 2, 4, 8}` with `./ring6_sweep.py --ns3-dir references/ns-3-dev --log-dir sweeps --tinfo 0 0.5 1 2 4 8 --modes global local --bad-arc 4,5 --bad-on 2 --bad-off 8 --count 6 --rate 1`. Global now recovers as soon as a snapshot fires during the fault window (0.5–2 s) and degrades back to 1 delivery when `Tinfo` stretches beyond the outage (4 s, 8 s). CSV + plots refreshed under `sweeps/`.
- **M7 (Done):** Added per-node buffers (`--B`) plus TX/DROP logging and new metrics (`queueDrops`, `wasteTx`). Validated with `./ring6_sweep.py --ns3-dir references/ns-3-dev --log-dir sweeps --tinfo 0 0.5 1 2 4 8 --modes global local --bad-arc 4,5 --bad-on 2 --bad-off 8 --count 6 --rate 1 --buffer 2` and refreshed plots for delivered/blocked/queueDrops/wasteTx.

---

*Prepared for quick iteration: each step compiles & runs, with a single new idea per milestone.*
