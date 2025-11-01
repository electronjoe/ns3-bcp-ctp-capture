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

