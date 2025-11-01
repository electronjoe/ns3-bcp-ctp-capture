# NS3-BCP-CTP-CAPTURE


## Env setup

```bash
git clone https://gitlab.com/nsnam/ns-3-dev.git
cd ns-3-dev
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/opt/ns3
cmake --build build -j$(nproc)
cmake --build build --target install
```

## Example NS3 Build and Run

```bash
cd example
cmake -S . -B build
cmake --build build -j$(nproc)
./build/line-lr-wpan
```

Running the binary generates output pcap files in the local directory (one per node).

```bash
electronjoe@pop-os:~/Documents/too-slow-to-know/simulation$ ls *.pcap
line-lr-wpan-0-0.pcap  line-lr-wpan-1-0.pcap  line-lr-wpan-2-0.pcap  line-lr-wpan-3-0.pcap
```

To clean:

```bash
  cmake --build build --target clean
```