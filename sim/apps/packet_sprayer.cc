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
  if (m_ev.IsPending()) ns3::Simulator::Cancel(m_ev);
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
