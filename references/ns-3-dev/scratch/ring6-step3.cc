/*
 * Copyright (c) 2011 The Boeing Company
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author:  Tom Henderson <thomas.r.henderson@boeing.com>
 */

/*
 * Milestone 3: add a tiny ring header and forward clockwise until the sink (node 0)
 * consumes packets emitted by a single source (default node 3).
 */
#include "ns3/constant-position-mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/header.h"
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
#include <string>

using namespace ns3;
using namespace ns3::lrwpan;

namespace
{
constexpr uint16_t kNumNodes = 6;
constexpr double kDefaultRingRadius = 15.0; // meters
constexpr double kTrafficStart = 1.0;       // seconds
constexpr double kTwoPi = 6.28318530717958647692;
} // namespace

class RingHeader : public Header
{
  public:
    RingHeader();
    RingHeader(uint8_t src, uint8_t dst, uint8_t ttl, uint32_t sequence);

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    uint32_t GetSerializedSize() const override;
    void Print(std::ostream& os) const override;

    void SetSrc(uint8_t src);
    void SetDst(uint8_t dst);
    void SetTtl(uint8_t ttl);
    void SetSequence(uint32_t sequence);

    uint8_t GetSrc() const;
    uint8_t GetDst() const;
    uint8_t GetTtl() const;
    uint32_t GetSequence() const;

  private:
    uint8_t m_src{0};
    uint8_t m_dst{0};
    uint8_t m_ttl{0};
    uint32_t m_sequence{0};
};

RingHeader::RingHeader() = default;

RingHeader::RingHeader(uint8_t src, uint8_t dst, uint8_t ttl, uint32_t sequence)
    : m_src(src),
      m_dst(dst),
      m_ttl(ttl),
      m_sequence(sequence)
{
}

TypeId
RingHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RingHeader")
                            .SetParent<Header>()
                            .SetGroupName("LrWpan")
                            .AddConstructor<RingHeader>();
    return tid;
}

TypeId
RingHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
RingHeader::Serialize(Buffer::Iterator start) const
{
    start.WriteU8(m_src);
    start.WriteU8(m_dst);
    start.WriteU8(m_ttl);
    start.WriteHtonU32(m_sequence);
}

uint32_t
RingHeader::Deserialize(Buffer::Iterator start)
{
    m_src = start.ReadU8();
    m_dst = start.ReadU8();
    m_ttl = start.ReadU8();
    m_sequence = start.ReadNtohU32();
    return GetSerializedSize();
}

uint32_t
RingHeader::GetSerializedSize() const
{
    return 7;
}

void
RingHeader::Print(std::ostream& os) const
{
    os << "src=" << static_cast<uint32_t>(m_src) << " dst=" << static_cast<uint32_t>(m_dst)
       << " ttl=" << static_cast<uint32_t>(m_ttl) << " seq=" << m_sequence;
}

void
RingHeader::SetSrc(uint8_t src)
{
    m_src = src;
}

void
RingHeader::SetDst(uint8_t dst)
{
    m_dst = dst;
}

void
RingHeader::SetTtl(uint8_t ttl)
{
    m_ttl = ttl;
}

void
RingHeader::SetSequence(uint32_t sequence)
{
    m_sequence = sequence;
}

uint8_t
RingHeader::GetSrc() const
{
    return m_src;
}

uint8_t
RingHeader::GetDst() const
{
    return m_dst;
}

uint8_t
RingHeader::GetTtl() const
{
    return m_ttl;
}

uint32_t
RingHeader::GetSequence() const
{
    return m_sequence;
}

struct RingConfig
{
    uint16_t sinkId{0};
    uint16_t sourceId{3};
    double ratePps{5.0};
    uint32_t packetCount{10};
    uint32_t payloadBytes{40};
    uint8_t ttl{kNumNodes};
};

struct ForwarderContext
{
    std::array<Ptr<LrWpanNetDevice>, kNumNodes>* devices{nullptr};
    std::array<Mac16Address, kNumNodes>* shortAddrs{nullptr};
    const RingConfig* config{nullptr};
    uint8_t nextMsduHandle{0};
    uint32_t nextSequence{0};
};

static uint16_t
ClockwiseNeighbor(uint16_t nodeIndex)
{
    return (nodeIndex + 1) % kNumNodes;
}

static void SendToClockwiseNeighbor(ForwarderContext* ctx,
                                    uint16_t nodeIndex,
                                    Ptr<Packet> packet);

static void
DataConfirm(uint16_t nodeIndex, McpsDataConfirmParams params)
{
    NS_LOG_UNCOND("Node " << nodeIndex
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
ForwardingIndication(ForwarderContext* ctx,
                     uint16_t nodeIndex,
                     McpsDataIndicationParams params,
                     Ptr<Packet> packet)
{
    RingHeader header;
    if (packet->RemoveHeader(header) == 0)
    {
        NS_LOG_UNCOND("Node " << nodeIndex << " received packet without RingHeader");
        return;
    }

    if (header.GetDst() == nodeIndex)
    {
        if (nodeIndex == ctx->config->sinkId)
        {
            uint32_t hops = (ctx->config->ttl - header.GetTtl()) + 1;
            NS_LOG_UNCOND("DELIVERED seq=" << header.GetSequence() << " src="
                                           << static_cast<uint32_t>(header.GetSrc())
                                           << " hops=" << hops);
        }
        else
        {
            NS_LOG_UNCOND("Node " << nodeIndex << " consumed seq=" << header.GetSequence());
        }
        return;
    }

    uint8_t ttl = header.GetTtl();
    if (ttl <= 1)
    {
        NS_LOG_UNCOND("Node " << nodeIndex << " dropping seq=" << header.GetSequence()
                              << " ttl exhausted");
        return;
    }

    header.SetTtl(ttl - 1);
    packet->AddHeader(header);
    uint16_t nextHop = ClockwiseNeighbor(nodeIndex);
    NS_LOG_UNCOND("Node " << nodeIndex << " forwarding seq=" << header.GetSequence()
                          << " -> " << nextHop << " ttl=" << static_cast<uint32_t>(header.GetTtl()));
    SendToClockwiseNeighbor(ctx, nodeIndex, packet);
}

static void
SendToClockwiseNeighbor(ForwarderContext* ctx, uint16_t nodeIndex, Ptr<Packet> packet)
{
    uint16_t nextHop = ClockwiseNeighbor(nodeIndex);
    Ptr<LrWpanNetDevice> device = ctx->devices->at(nodeIndex);

    McpsDataRequestParams params;
    params.m_dstPanId = 0;
    params.m_srcAddrMode = SHORT_ADDR;
    params.m_dstAddrMode = SHORT_ADDR;
    params.m_dstAddr = ctx->shortAddrs->at(nextHop);
    params.m_msduHandle = ctx->nextMsduHandle++;
    params.m_txOptions = TX_OPTION_ACK;

    Simulator::ScheduleWithContext(device->GetNode()->GetId(),
                                   Seconds(0),
                                   &LrWpanMac::McpsDataRequest,
                                   device->GetMac(),
                                   params,
                                   packet);
}

static void
GenerateSourceTraffic(ForwarderContext* ctx,
                      const RingConfig* config,
                      uint32_t remaining,
                      Time interval)
{
    if (remaining == 0)
    {
        return;
    }

    Ptr<Packet> packet = Create<Packet>(config->payloadBytes);
    RingHeader header(config->sourceId,
                      config->sinkId,
                      config->ttl,
                      ctx->nextSequence++);
    packet->AddHeader(header);
    NS_LOG_UNCOND("Source " << config->sourceId << " injecting seq=" << header.GetSequence());
    SendToClockwiseNeighbor(ctx, config->sourceId, packet);

    if (remaining > 1)
    {
        Simulator::Schedule(interval,
                            &GenerateSourceTraffic,
                            ctx,
                            config,
                            remaining - 1,
                            interval);
    }
}

int
main(int argc, char* argv[])
{
    bool verbose = false;
    double radius = kDefaultRingRadius;
    RingConfig config;

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "turn on all log components", verbose);
    cmd.AddValue("radius", "ring radius in meters", radius);
    cmd.AddValue("source", "source node index (0-5)", config.sourceId);
    cmd.AddValue("sink", "sink node index (0-5)", config.sinkId);
    cmd.AddValue("rate", "source packet rate (packets per second)", config.ratePps);
    cmd.AddValue("count", "number of application packets to send", config.packetCount);
    cmd.AddValue("payload", "application payload size in bytes", config.payloadBytes);
    cmd.AddValue("ttl", "initial TTL carried in RingHeader", config.ttl);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnableAll(LogLevel(LOG_PREFIX_TIME | LOG_PREFIX_FUNC));
        LogComponentEnable("LrWpanPhy", LOG_LEVEL_ALL);
        LogComponentEnable("LrWpanMac", LOG_LEVEL_ALL);
    }

    NS_ABORT_MSG_IF(config.sourceId >= kNumNodes, "sourceId must be within ring");
    NS_ABORT_MSG_IF(config.sinkId >= kNumNodes, "sinkId must be within ring");
    NS_ABORT_MSG_IF(config.sourceId == config.sinkId, "sourceId must differ from sinkId");
    NS_ABORT_MSG_IF(config.ttl == 0, "ttl must be >= 1");

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
    const std::array<std::string, kNumNodes> shortAddrStrings = {
        {"00:01", "00:02", "00:03", "00:04", "00:05", "00:06"}};
    std::array<Mac16Address, kNumNodes> shortAddresses{};

    ForwarderContext ctx;
    ctx.devices = &devices;
    ctx.shortAddrs = &shortAddresses;
    ctx.config = &config;

    for (uint16_t i = 0; i < kNumNodes; ++i)
    {
        devices[i] = CreateObject<LrWpanNetDevice>();
        devices[i]->SetChannel(channel);
        nodes.Get(i)->AddDevice(devices[i]);

        std::ostringstream extAddr;
        extAddr << "00:00:00:00:00:00:00:" << std::uppercase << std::setw(2) << std::setfill('0')
                << std::hex << (i + 1);
        devices[i]->GetMac()->SetExtendedAddress(Mac64Address(extAddr.str().c_str()));
        shortAddresses[i] = Mac16Address(shortAddrStrings[i].c_str());
        devices[i]->GetMac()->SetShortAddress(shortAddresses[i]);

        std::ostringstream phyCtx;
        phyCtx << "phy" << i;
        devices[i]->GetPhy()->TraceConnect("TrxState",
                                           phyCtx.str(),
                                           MakeCallback(&StateChangeNotification));

        Ptr<ConstantPositionMobilityModel> mobility = CreateObject<ConstantPositionMobilityModel>();
        double angle = (kTwoPi * i) / static_cast<double>(kNumNodes);
        Vector position(radius * std::cos(angle), radius * std::sin(angle), 0.0);
        mobility->SetPosition(position);
        devices[i]->GetPhy()->SetMobility(mobility);

        devices[i]->GetMac()->SetMcpsDataIndicationCallback(
            MakeBoundCallback(&ForwardingIndication, &ctx, i));
        devices[i]->GetMac()->SetMcpsDataConfirmCallback(MakeBoundCallback(&DataConfirm, i));
    }

    Time interval = Seconds(config.ratePps > 0 ? 1.0 / config.ratePps : 1.0);
    if (config.packetCount > 0)
    {
        Simulator::Schedule(Seconds(kTrafficStart),
                            &GenerateSourceTraffic,
                            &ctx,
                            &config,
                            config.packetCount,
                            interval);
    }

    double trafficWindow = 0.0;
    if (config.packetCount > 0 && config.ratePps > 0)
    {
        trafficWindow = static_cast<double>(config.packetCount - 1) / config.ratePps;
    }
    Simulator::Stop(Seconds(kTrafficStart + trafficWindow + 5.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
