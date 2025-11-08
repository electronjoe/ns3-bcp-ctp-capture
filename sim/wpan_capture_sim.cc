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
#include "ns3/packet-socket-helper.h"
#include "ns3/packet-socket-address.h"
#include "apps/packet_sprayer.h"
#include "apps/raw_l2_sink.h"
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

  // Set the noise PSD with explicit thermal noise + NF
  // Noise Factor = 10^(NF_dB / 10) - this is a linear ratio, not dB
  double noiseFactor = std::pow(10.0, noiseFigureDb / 10.0);
  helper.SetNoiseFactor(noiseFactor);
  Ptr<SpectrumValue> noise = helper.CreateNoisePowerSpectralDensity(channelNumber);
  phy->SetNoisePowerSpectralDensity(noise);

  // Note: We do NOT disable MAC retries when measuring at PHY level
  // PHY traces capture all reception attempts, including retransmissions
  // MAC retries are needed for proper link operation (ACKs, etc.)
}


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

    // Set MAC retries to 0 for pure PER measurement in calibration
    // Note: Setting to 0 may disable ACKs entirely; use 1-2 for single-attempt with ACKs
    dev->GetMac()->SetMacMaxFrameRetries(1);
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
    csv << "distance_m,sinr_db,offered,received,per\n";

    for (double d : cfg.distances) {
      auto scene = MakeScene(2, cfg);
      // Positions: node0 @ (0,0,0), node1 @ (d,0,0)
      scene.nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0,0,0));
      scene.nodes.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(d,0,0));

      // L2 PacketSocket path: direct frame transmission without IP stack delays
      using namespace calib;

      // Install PacketSocketFactory on nodes (needed for PacketSocket)
      PacketSocketHelper packetSocket;
      packetSocket.Install(scene.nodes);

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
                        /*count*/ cfg.packetsPerPoint, /*interval*/ MilliSeconds(2));
      scene.nodes.Get(0)->AddApplication(sprayer);
      sprayer->SetStartTime(Seconds(5.0));
      sprayer->SetStopTime(Seconds(25.0));

      // Compute mean SINR (no interferers)
      auto mm0 = scene.nodes.Get(0)->GetObject<MobilityModel>();
      auto mm1 = scene.nodes.Get(1)->GetObject<MobilityModel>();
      double sinrDb = ComputeSinrDb(scene.loss, mm0, mm1, {}, cfg.txPowerDbm, cfg.bandwidthHz, cfg.noiseFigureDb);

      Simulator::Stop(Seconds(30.0));
      Simulator::Run();
      Simulator::Destroy();

      uint32_t offered = sprayer->Sent();
      uint32_t received = sink->Received();
      double per = (offered > 0) ? 1.0 - (double)received / (double)offered : 0.0;

      csv << std::fixed << std::setprecision(2)
          << d << "," << sinrDb << "," << offered << "," << received << "," << per << "\n";
    }
    csv.close();
  }
  else if (mode == "capture-test") {
    // Three nodes: 0->1 (desired), 2 interferer near 1. Sweep P2-P0 around captureDb.
    std::ofstream csv(cfg.outDir + "/capture_toggle.csv");
    csv << "delta_db,offered,received,per\n";

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

      // L2 PacketSocket path
      using namespace calib;

      // Install PacketSocketFactory on nodes (needed for PacketSocket)
      PacketSocketHelper packetSocket;
      packetSocket.Install(scene.nodes);

      // L2 sink on receiver
      auto sink = CreateObject<RawL2SinkApp>();
      scene.nodes.Get(1)->AddApplication(sink);
      sink->SetStartTime(Seconds(0.5));
      sink->SetStopTime(Seconds(30.0));

      // Desired transmitter (node 0) sprayer
      auto sprayer = CreateObject<PacketSprayerApp>();
      auto dev1 = DynamicCast<lrwpan::LrWpanNetDevice>(scene.devs.Get(1));
      sprayer->Configure(dev0, dev1->GetAddress(), /*pktSize*/40,
                        /*count*/ cfg.packetsPerPoint, /*interval*/ MilliSeconds(2));
      scene.nodes.Get(0)->AddApplication(sprayer);
      sprayer->SetStartTime(Seconds(5.0));
      sprayer->SetStopTime(Seconds(25.0));

      // Interferer (node 2) sprayer - high rate
      auto interferer = CreateObject<PacketSprayerApp>();
      interferer->Configure(dev2, dev1->GetAddress(), /*pktSize*/40,
                           /*count*/ 10000, /*interval*/ MicroSeconds(100)); // ~10kpps
      scene.nodes.Get(2)->AddApplication(interferer);
      interferer->SetStartTime(Seconds(5.0));
      interferer->SetStopTime(Seconds(25.0));

      Simulator::Stop(Seconds(30.0));
      Simulator::Run();
      Simulator::Destroy();

      uint32_t offered = sprayer->Sent();
      uint32_t received = sink->Received();
      double per = (offered > 0) ? 1.0 - (double)received / (double)offered : 1.0;

      csv << ddb << "," << offered << "," << received << "," << per << "\n";
    }
    csv.close();
  }
  return 0;
}
