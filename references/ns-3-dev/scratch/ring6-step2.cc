/*
 * Copyright (c) 2011 The Boeing Company
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author:  Tom Henderson <thomas.r.henderson@boeing.com>
 */

/*
 * Milestone 2: build a 6-node lr-wpan ring where every neighbor pair
 * exchanges packets in both clockwise and counter-clockwise directions.
 */
#include "ns3/constant-position-mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/log.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/node-container.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/simulator.h"
#include "ns3/single-model-spectrum-channel.h"

#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace ns3;
using namespace ns3::lrwpan;

namespace
{
constexpr uint16_t kNumNodes = 6;
constexpr double kDefaultRingRadius = 15.0; // meters
constexpr double kStartTime = 1.0;          // seconds
constexpr double kTimeGap = 0.5;            // seconds between neighbor tests
constexpr double kTwoPi = 6.28318530717958647692;
} // namespace

static void
DataIndication(uint16_t nodeId, McpsDataIndicationParams params, Ptr<Packet> p)
{
    NS_LOG_UNCOND("Node " << nodeId << " received " << p->GetSize()
                          << " bytes from " << params.m_srcAddr);
}

static void
DataConfirm(uint16_t nodeId, McpsDataConfirmParams params)
{
    NS_LOG_UNCOND("Node " << nodeId
                          << " confirm status = " << static_cast<uint16_t>(params.m_status));
}

static void
StateChangeNotification(std::string context,
                        Time now,
                        PhyEnumeration oldState,
                        PhyEnumeration newState)
{
    NS_LOG_UNCOND(context << " state change at " << now.As(Time::S) << " from "
                          << LrWpanHelper::LrWpanPhyEnumerationPrinter(oldState) << " to "
                          << LrWpanHelper::LrWpanPhyEnumerationPrinter(newState));
}

static void
ScheduleNeighborSend(Time sendTime,
                     Ptr<LrWpanNetDevice> sender,
                     Mac16Address dst,
                     uint8_t msduHandle,
                     uint32_t context,
                     uint32_t payloadSize)
{
    Ptr<Packet> packet = Create<Packet>(payloadSize);
    McpsDataRequestParams params;
    params.m_dstPanId = 0;
    params.m_srcAddrMode = SHORT_ADDR;
    params.m_dstAddrMode = SHORT_ADDR;
    params.m_dstAddr = dst;
    params.m_msduHandle = msduHandle;
    params.m_txOptions = TX_OPTION_ACK;

    Simulator::ScheduleWithContext(context,
                                   sendTime,
                                   &LrWpanMac::McpsDataRequest,
                                   sender->GetMac(),
                                   params,
                                   packet);
}

int
main(int argc, char* argv[])
{
    bool verbose = false;
    double radius = kDefaultRingRadius;

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "turn on all log components", verbose);
    cmd.AddValue("radius", "ring radius in meters", radius);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnableAll(LogLevel(LOG_PREFIX_TIME | LOG_PREFIX_FUNC));
        LogComponentEnable("LrWpanPhy", LOG_LEVEL_ALL);
        LogComponentEnable("LrWpanMac", LOG_LEVEL_ALL);
    }

    NodeContainer nodes;
    nodes.Create(kNumNodes);

    Ptr<SingleModelSpectrumChannel> channel = CreateObject<SingleModelSpectrumChannel>();
    Ptr<LogDistancePropagationLossModel> propModel =
        CreateObject<LogDistancePropagationLossModel>();
    Ptr<ConstantSpeedPropagationDelayModel> delayModel =
        CreateObject<ConstantSpeedPropagationDelayModel>();
    channel->AddPropagationLossModel(propModel);
    channel->SetPropagationDelayModel(delayModel);

    std::array<Ptr<LrWpanNetDevice>, kNumNodes> devices{};
    constexpr std::array<const char*, kNumNodes> kShortAddresses = {
        {"00:01", "00:02", "00:03", "00:04", "00:05", "00:06"}};

    for (uint16_t i = 0; i < kNumNodes; ++i)
    {
        devices[i] = CreateObject<LrWpanNetDevice>();
        devices[i]->SetChannel(channel);
        nodes.Get(i)->AddDevice(devices[i]);

        std::ostringstream extAddr;
        extAddr << "00:00:00:00:00:00:00:" << std::uppercase << std::setw(2) << std::setfill('0')
                << std::hex << (i + 1);
        devices[i]->GetMac()->SetExtendedAddress(Mac64Address(extAddr.str().c_str()));
        devices[i]->GetMac()->SetShortAddress(Mac16Address(kShortAddresses[i]));

        std::ostringstream ctx;
        ctx << "phy" << i;
        devices[i]->GetPhy()->TraceConnect("TrxState", ctx.str(), MakeCallback(&StateChangeNotification));

        Ptr<ConstantPositionMobilityModel> mobility = CreateObject<ConstantPositionMobilityModel>();
        double angle = (kTwoPi * i) / static_cast<double>(kNumNodes);
        Vector position(radius * std::cos(angle), radius * std::sin(angle), 0.0);
        mobility->SetPosition(position);
        devices[i]->GetPhy()->SetMobility(mobility);

        devices[i]->GetMac()->SetMcpsDataIndicationCallback(
            MakeBoundCallback(&DataIndication, nodes.Get(i)->GetId()));
        devices[i]->GetMac()->SetMcpsDataConfirmCallback(
            MakeBoundCallback(&DataConfirm, nodes.Get(i)->GetId()));
    }

    double scheduledTime = kStartTime;
    uint8_t msduHandle = 0;
    const uint32_t payloadBase = 40; // add node index so payloads differ slightly

    for (uint16_t i = 0; i < kNumNodes; ++i)
    {
        uint16_t cw = (i + 1) % kNumNodes;

        ScheduleNeighborSend(Seconds(scheduledTime),
                             devices[i],
                             Mac16Address(kShortAddresses[cw]),
                             msduHandle++,
                             nodes.Get(i)->GetId(),
                             payloadBase + i);
        scheduledTime += kTimeGap;

        ScheduleNeighborSend(Seconds(scheduledTime),
                             devices[cw],
                             Mac16Address(kShortAddresses[i]),
                             msduHandle++,
                             nodes.Get(cw)->GetId(),
                             payloadBase + cw);
        scheduledTime += kTimeGap;
    }

    Simulator::Stop(Seconds(scheduledTime + 1.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
