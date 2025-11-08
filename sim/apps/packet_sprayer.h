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
