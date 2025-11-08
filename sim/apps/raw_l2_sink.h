#pragma once
#include "ns3/application.h"
#include "ns3/socket.h"

namespace calib {
class RawL2SinkApp : public ns3::Application {
public:
  static ns3::TypeId GetTypeId (void);
  RawL2SinkApp();
  uint32_t Received() const { return m_rx; }
protected:
  void StartApplication() override;
  void StopApplication() override;
private:
  void HandleRead(ns3::Ptr<ns3::Socket> sock);
  ns3::Ptr<ns3::Socket> m_sock;
  uint32_t m_rx{0};
};
}
