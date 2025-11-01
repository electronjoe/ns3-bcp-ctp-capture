// sim/wpan_capture_sim.cc
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/propagation-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/lr-wpan-spectrum-value-helper.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include <fstream>
#include <iomanip>

using namespace ns3;
using namespace ns3::lrwpan;
using std::string;

NS_LOG_COMPONENT_DEFINE("WpanCaptureCalib");

struct CalibCfg {
  double betaDb = 7.0;
  double captureDb = 6.0;
  double txPowerDbm = 0.0;
  double noiseFigureDb = 10.0;
  double bandwidthHz = 2e6;
  double centerFreqHz = 2.405e9;
  // propagation
  double referenceLossDb = 40.0;
  double exponent = 3.0;
  double referenceDistance = 1.0;
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

// dBm/Hz to W/Hz conversion
static double
DbmPerHzToWPerHz(double dbmPerHz) { return std::pow(10.0, (dbmPerHz - 30.0)/10.0); }

// Set per-device TX power and noise PSD using LrWpanSpectrumValueHelper
static void
SetTxDbmAndNoise(Ptr<LrWpanNetDevice> dev,
                 double txDbm,
                 uint8_t channelNumber,      // 11..26 (2.4 GHz)
                 double noiseFigureDb,       // e.g., 10
                 double bandwidthHz)         // e.g., 2e6
{
  Ptr<LrWpanPhy> phy = dev->GetPhy();
  LrWpanSpectrumValueHelper helper;

  // Note: Channel is already configured by LrWpanHelper (default channel 11, page 0)
  // We just create the appropriate PSD for that channel

  // TX PSD shaped for 802.15.4 channel: integral ≈ total TX power (W)
  double txW = DbmToW(txDbm);
  Ptr<SpectrumValue> txPsd = helper.CreateTxPowerSpectralDensity(txW, channelNumber);
  phy->SetTxPowerSpectralDensity(txPsd);

  // Set the noise PSD - uses default thermal noise for the channel
  Ptr<SpectrumValue> noise = helper.CreateNoisePowerSpectralDensity(channelNumber);
  phy->SetNoisePowerSpectralDensity(noise);
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
  Ipv6InterfaceContainer ifaces;
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
  s.loss->SetReference(cfg.referenceDistance, cfg.referenceLossDb);
  s.loss->SetPathLossExponent(cfg.exponent);
  s.channel->AddPropagationLossModel(s.loss);
  s.channel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());

  LrWpanHelper wpan;
  wpan.SetChannel(s.channel);
  s.devs = wpan.Install(s.nodes);

  const uint16_t kPanId = 0x0AAA;
  const uint8_t channelNumber = 11;  // 2.4 GHz channel 11
  for (uint32_t i = 0; i < s.devs.GetN(); ++i) {
    auto dev = DynamicCast<lrwpan::LrWpanNetDevice>(s.devs.Get(i));
    dev->SetAddress(Mac64Address::Allocate());
    dev->GetMac()->SetPanId(kPanId);
    dev->GetMac()->SetShortAddress(Mac16Address::Allocate());

    // Set transmit power using LrWpanSpectrumValueHelper
    SetTxDbmAndNoise(dev, cfg.txPowerDbm, channelNumber, cfg.noiseFigureDb, cfg.bandwidthHz);
  }

  // IPv6 stack for simple UDP echo
  InternetStackHelper internet; internet.Install(s.nodes);
  SixLowPanHelper six; auto sixDevs = six.Install(s.devs);
  Ipv6AddressHelper ipv6; ipv6.SetBase("2001:db8:calib::", 64);
  s.ifaces = ipv6.Assign(sixDevs);
  for (uint32_t i=0;i<s.ifaces.GetN();++i){
    s.ifaces.SetForwarding(i,true);
    s.ifaces.SetDefaultRouteInAllNodes(i);
  }

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

  CommandLine cmd;
  cmd.AddValue("mode", "per-sinr-sweep | capture-test", mode);
  cmd.AddValue("betaDb", "SINR threshold beta (dB)", cfg.betaDb);
  cmd.AddValue("captureDb", "capture margin x (dB)", cfg.captureDb);
  cmd.AddValue("txPowerDbm", "Tx power (dBm)", cfg.txPowerDbm);
  cmd.AddValue("noiseFigureDb", "Noise figure (dB)", cfg.noiseFigureDb);
  cmd.AddValue("bandwidthHz", "Receiver bandwidth (Hz)", cfg.bandwidthHz);
  cmd.AddValue("out", "Output directory", cfg.outDir);
  cmd.AddValue("refLossDb", "LogDistance reference loss at 1m (dB)", cfg.referenceLossDb);
  cmd.AddValue("exponent", "LogDistance exponent", cfg.exponent);
  cmd.AddValue("refDist", "LogDistance reference distance (m)", cfg.referenceDistance);
  cmd.Parse(argc, argv);

  // Embed sweep distances if not provided via YAML (Phase 1 keeps it simple)
  if (cfg.distances.empty()) {
    cfg.distances = {6,8,10,12,14,16,18,20};
  }

  // Create output dir
  if (cfg.outDir.size()) {
    std::string mkdir = "mkdir -p " + cfg.outDir;
    int ret = system(mkdir.c_str());
    (void)ret; // suppress unused warning
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

      Ipv6Address dst = scene.ifaces.GetAddress(1, 1); // global address of node 1
      UdpEchoClientHelper client(dst, port);
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

      // Set desired transmitter to base power (already set in MakeScene)
      // Set interferer power to base + delta
      SetTxDbmAndNoise(dev2, cfg.txPowerDbm + ddb, 11, cfg.noiseFigureDb, cfg.bandwidthHz);

      // Start desired UDP flow 0->1 (echo)
      uint16_t port = 9;
      UdpEchoServerHelper server(port);
      auto appsS = server.Install(scene.nodes.Get(1));
      appsS.Start(Seconds(0.5)); appsS.Stop(Seconds(12.0));

      Ipv6Address dst = scene.ifaces.GetAddress(1, 1); // global address of node 1
      UdpEchoClientHelper client(dst, port);
      client.SetAttribute("MaxPackets", UintegerValue(cfg.packetsPerPoint));
      client.SetAttribute("Interval", TimeValue(MilliSeconds(10)));
      client.SetAttribute("PacketSize", UintegerValue(40));
      auto appsC = client.Install(scene.nodes.Get(0));
      appsC.Start(Seconds(1.0)); appsC.Stop(Seconds(12.0));

      // Interferer 2 sends CBR to RX 1 concurrently
      OnOffHelper interferer("ns3::UdpSocketFactory", Address(Inet6SocketAddress(dst, port)));
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
