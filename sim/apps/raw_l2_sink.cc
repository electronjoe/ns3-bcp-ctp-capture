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
