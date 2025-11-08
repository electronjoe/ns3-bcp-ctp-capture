/*
 * Copyright (c) 2011 The Boeing Company
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author:  Tom Henderson <thomas.r.henderson@boeing.com>
 */

/*
 * Milestone 4: extend the multi-hop ring with controller modes that choose
 * next hops either from a static snapshot route (global) or a myopic
 * per-transmission decision (local).
 */
#include "ns3/constant-position-mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/double.h"
#include "ns3/header.h"
#include "ns3/log.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/node-container.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/single-model-spectrum-channel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace ns3;
using namespace ns3::lrwpan;

namespace
{
constexpr uint16_t kNumNodes = 6;
constexpr double kDefaultRingRadius = 15.0; // meters
constexpr double kTrafficStart = 1.0;       // seconds
constexpr double kTwoPi = 6.28318530717958647692;
} // namespace

enum class ControllerMode
{
    GLOBAL,
    LOCAL
};

enum class RouteDirection : uint8_t
{
    AUTO = 0,
    CW = 1,
    CCW = 2
};

struct PendingTx
{
    Ptr<Packet> packet;
    uint16_t neighbor{0};
    uint32_t sequence{0};
};

struct PacketHistoryEntry
{
    uint32_t txCount{0};
    bool delivered{false};
    bool dropped{false};
};

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
    void SetDirection(RouteDirection dir);

    uint8_t GetSrc() const;
    uint8_t GetDst() const;
    uint8_t GetTtl() const;
    uint32_t GetSequence() const;
    RouteDirection GetDirection() const;

  private:
    uint8_t m_src{0};
    uint8_t m_dst{0};
    uint8_t m_ttl{0};
    uint32_t m_sequence{0};
    RouteDirection m_direction{RouteDirection::AUTO};
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
    start.WriteU8(static_cast<uint8_t>(m_direction));
}

uint32_t
RingHeader::Deserialize(Buffer::Iterator start)
{
    m_src = start.ReadU8();
    m_dst = start.ReadU8();
    m_ttl = start.ReadU8();
    m_sequence = start.ReadNtohU32();
    m_direction = static_cast<RouteDirection>(start.ReadU8());
    return GetSerializedSize();
}

uint32_t
RingHeader::GetSerializedSize() const
{
    return 8;
}

void
RingHeader::Print(std::ostream& os) const
{
    os << "src=" << static_cast<uint32_t>(m_src) << " dst=" << static_cast<uint32_t>(m_dst)
       << " ttl=" << static_cast<uint32_t>(m_ttl) << " seq=" << m_sequence
       << " dir=" << static_cast<uint32_t>(m_direction);
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

void
RingHeader::SetDirection(RouteDirection dir)
{
    m_direction = dir;
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

RouteDirection
RingHeader::GetDirection() const
{
    return m_direction;
}
struct RingConfig
{
    uint16_t sinkId{0};
    uint16_t sourceId{3};
    double ratePps{5.0};
    uint32_t packetCount{10};
    uint32_t payloadBytes{40};
    uint8_t ttl{kNumNodes};
    double snapshotPeriod{0.0}; // seconds, 0 disables periodic snapshots
    uint32_t bufferCapacity{5};
    double simTime{0.0};
    bool hasFault{false};
    uint16_t faultFrom{0};
    uint16_t faultTo{0};
    double faultOn{0.0};
    double faultOff{0.0};
    bool randomFaults{false};
    double randomFaultOnMean{5.0};
    double randomFaultOffMean{5.0};
    double randomFaultStart{0.0};
    int64_t randomFaultStream{1};
};

struct Metrics
{
    uint32_t delivered{0};
    uint32_t ttlDrops{0};
    uint32_t noRouteDrops{0};
    uint32_t blockedTransmissions{0};
    uint32_t queueDrops{0};
    uint32_t wasteTx{0};
};

struct ForwarderContext
{
    std::array<Ptr<LrWpanNetDevice>, kNumNodes>* devices{nullptr};
    std::array<Mac16Address, kNumNodes>* shortAddrs{nullptr};
    const RingConfig* config{nullptr};
    ControllerMode mode{ControllerMode::GLOBAL};
    std::array<uint16_t, kNumNodes> parent{0};
    std::vector<std::tuple<uint16_t, uint16_t, double, double>> faults;
    uint8_t nextMsduHandle{0};
    uint32_t nextSequence{0};
    Metrics metrics;
    std::array<std::deque<PendingTx>, kNumNodes> txQueues;
    std::array<bool, kNumNodes> txBusy{};
    std::array<uint32_t, kNumNodes> inFlightSeq{0};
    std::unordered_map<uint32_t, PacketHistoryEntry> packetHistory;
};

static uint16_t
ClockwiseNeighbor(uint16_t nodeIndex)
{
    return (nodeIndex + 1) % kNumNodes;
}

static uint16_t
CounterClockwiseNeighbor(uint16_t nodeIndex)
{
    return (nodeIndex + kNumNodes - 1) % kNumNodes;
}

static bool LinkAllowed(const ForwarderContext* ctx, uint16_t from, uint16_t to);
static bool PathToSinkAllowed(const ForwarderContext* ctx, uint16_t start, RouteDirection dir);
static void InstallSnapshotParents(ForwarderContext* ctx);
static void ScheduleSnapshotRefresh(ForwarderContext* ctx, double period);
static void RegisterPacket(ForwarderContext* ctx, uint32_t sequence);
static void RecordTxEvent(ForwarderContext* ctx, uint32_t sequence);
static void RecordDeliveryEvent(ForwarderContext* ctx, uint32_t sequence);
static void RecordDropEvent(ForwarderContext* ctx, uint32_t sequence, const std::string& reason, uint16_t nodeIndex);
static uint16_t SelectNextHop(ForwarderContext* ctx, uint16_t nodeIndex, RingHeader* header);
static bool EnqueueTransmission(ForwarderContext* ctx,
                                uint16_t nodeIndex,
                                uint16_t neighborIndex,
                                Ptr<Packet> packet,
                                uint32_t sequence);
static void TryTransmitNext(ForwarderContext* ctx, uint16_t nodeIndex);
static void HandleTxComplete(ForwarderContext* ctx, uint16_t nodeIndex);
static void SendToNeighbor(ForwarderContext* ctx,
                           uint16_t nodeIndex,
                           uint16_t neighborIndex,
                           Ptr<Packet> packet,
                           uint32_t sequence);
static void GenerateRandomFaultWindows(ForwarderContext* ctx, double startTime, double simStop);
static void ReportStats(const ForwarderContext* ctx, const std::string& modeStr);
static void ReportStats(const ForwarderContext* ctx, const std::string& modeStr);

static void
DataConfirm(ForwarderContext* ctx, uint16_t nodeIndex, McpsDataConfirmParams params)
{
    NS_LOG_UNCOND("Node " << nodeIndex
                          << " confirm status = " << static_cast<uint16_t>(params.m_status));
    HandleTxComplete(ctx, nodeIndex);
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
    (void)params;
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
            ctx->metrics.delivered++;
            RecordDeliveryEvent(ctx, header.GetSequence());
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
        ctx->metrics.ttlDrops++;
        RecordDropEvent(ctx, header.GetSequence(), "ttl", nodeIndex);
        return;
    }

    header.SetTtl(ttl - 1);
    uint16_t nextHop = SelectNextHop(ctx, nodeIndex, &header);
    if (nextHop == nodeIndex)
    {
        ctx->metrics.noRouteDrops++;
        RecordDropEvent(ctx, header.GetSequence(), "no_route", nodeIndex);
        return;
    }
    packet->AddHeader(header);
    NS_LOG_UNCOND("Node " << nodeIndex << " forwarding seq=" << header.GetSequence()
                          << " -> " << nextHop << " ttl=" << static_cast<uint32_t>(header.GetTtl()));
    SendToNeighbor(ctx, nodeIndex, nextHop, packet, header.GetSequence());
}

static bool
LinkAllowed(const ForwarderContext* ctx, uint16_t from, uint16_t to)
{
    Time now = Simulator::Now();
    double t = now.GetSeconds();
    if (ctx->faults.empty())
    {
        return true;
    }

    for (const auto& entry : ctx->faults)
    {
        uint16_t badFrom;
        uint16_t badTo;
        double start;
        double stop;
        std::tie(badFrom, badTo, start, stop) = entry;
        if (badFrom == from && badTo == to)
        {
            if (t >= start && t < stop)
            {
                return false;
            }
        }
    }
    return true;
}

static bool
PathToSinkAllowed(const ForwarderContext* ctx, uint16_t start, RouteDirection dir)
{
    uint16_t sink = ctx->config->sinkId;
    if (start == sink)
    {
        return true;
    }

    uint16_t current = start;
    uint16_t guard = 0;
    while (current != sink && guard < kNumNodes)
    {
        uint16_t next =
            (dir == RouteDirection::CW) ? ClockwiseNeighbor(current) : CounterClockwiseNeighbor(current);
        if (!LinkAllowed(ctx, current, next))
        {
            return false;
        }
        current = next;
        guard++;
    }
    return current == sink;
}

static void
InstallSnapshotParents(ForwarderContext* ctx)
{
    uint16_t sink = ctx->config->sinkId;
    for (uint16_t i = 0; i < kNumNodes; ++i)
    {
        ctx->parent[i] = i;
    }
    ctx->parent[sink] = sink;

    for (uint16_t node = 0; node < kNumNodes; ++node)
    {
        if (node == sink)
        {
            continue;
        }
        if (PathToSinkAllowed(ctx, node, RouteDirection::CW))
        {
            ctx->parent[node] = ClockwiseNeighbor(node);
            continue;
        }
        if (PathToSinkAllowed(ctx, node, RouteDirection::CCW))
        {
            ctx->parent[node] = CounterClockwiseNeighbor(node);
            continue;
        }
        // leave parent[node] == node when neither direction currently reaches sink
    }
}

static void
RegisterPacket(ForwarderContext* ctx, uint32_t sequence)
{
    ctx->packetHistory[sequence] = PacketHistoryEntry{};
}

static void
RecordTxEvent(ForwarderContext* ctx, uint32_t sequence)
{
    ctx->packetHistory[sequence].txCount++;
}

static void
RecordDeliveryEvent(ForwarderContext* ctx, uint32_t sequence)
{
    auto& entry = ctx->packetHistory[sequence];
    entry.delivered = true;
}

static void
RecordDropEvent(ForwarderContext* ctx, uint32_t sequence, const std::string& reason, uint16_t nodeIndex)
{
    auto& entry = ctx->packetHistory[sequence];
    if (entry.delivered || entry.dropped)
    {
        return;
    }
    entry.dropped = true;
    ctx->metrics.wasteTx += entry.txCount;
    NS_LOG_UNCOND("DROP seq=" << sequence << " node=" << nodeIndex << " reason=" << reason
                              << " tx=" << entry.txCount);
}

static bool
EnqueueTransmission(ForwarderContext* ctx,
                    uint16_t nodeIndex,
                    uint16_t neighborIndex,
                    Ptr<Packet> packet,
                    uint32_t sequence)
{
    uint32_t capacity = ctx->config->bufferCapacity;
    auto& queue = ctx->txQueues[nodeIndex];
    uint32_t inService = ctx->txBusy[nodeIndex] ? 1u : 0u;
    if (capacity > 0 && queue.size() + inService >= capacity)
    {
        ctx->metrics.queueDrops++;
        RecordDropEvent(ctx, sequence, "queue_full", nodeIndex);
        return false;
    }

    queue.push_back(PendingTx{packet, neighborIndex, sequence});
    TryTransmitNext(ctx, nodeIndex);
    return true;
}

static void
TryTransmitNext(ForwarderContext* ctx, uint16_t nodeIndex)
{
    if (ctx->txBusy[nodeIndex])
    {
        return;
    }

    auto& queue = ctx->txQueues[nodeIndex];
    while (!queue.empty())
    {
        PendingTx tx = queue.front();
        queue.pop_front();

        if (!LinkAllowed(ctx, nodeIndex, tx.neighbor))
        {
            ctx->metrics.blockedTransmissions++;
            RecordDropEvent(ctx, tx.sequence, "blocked", nodeIndex);
            continue;
        }

        Ptr<LrWpanNetDevice> device = ctx->devices->at(nodeIndex);
        McpsDataRequestParams params;
        params.m_dstPanId = 0;
        params.m_srcAddrMode = SHORT_ADDR;
        params.m_dstAddrMode = SHORT_ADDR;
        params.m_dstAddr = ctx->shortAddrs->at(tx.neighbor);
        params.m_msduHandle = ctx->nextMsduHandle++;
        params.m_txOptions = TX_OPTION_ACK;

        ctx->txBusy[nodeIndex] = true;
        ctx->inFlightSeq[nodeIndex] = tx.sequence;
        RecordTxEvent(ctx, tx.sequence);
        NS_LOG_UNCOND("TX seq=" << tx.sequence << " from=" << nodeIndex << " to=" << tx.neighbor
                                << " queueDepth=" << queue.size());

        Simulator::ScheduleWithContext(device->GetNode()->GetId(),
                                       Seconds(0),
                                       &LrWpanMac::McpsDataRequest,
                                       device->GetMac(),
                                       params,
                                       tx.packet);
        break;
    }
}

static void
HandleTxComplete(ForwarderContext* ctx, uint16_t nodeIndex)
{
    ctx->txBusy[nodeIndex] = false;
    ctx->inFlightSeq[nodeIndex] = 0;
    TryTransmitNext(ctx, nodeIndex);
}

static void
SendToNeighbor(ForwarderContext* ctx,
               uint16_t nodeIndex,
               uint16_t neighborIndex,
               Ptr<Packet> packet,
               uint32_t sequence)
{
    EnqueueTransmission(ctx, nodeIndex, neighborIndex, packet, sequence);
}

static void
GenerateRandomFaultWindows(ForwarderContext* ctx, double startTime, double simStop)
{
    if (!ctx->config->randomFaults || !ctx->config->hasFault)
    {
        return;
    }

    double begin = std::max(0.0, startTime);
    if (simStop <= begin)
    {
        return;
    }

    Ptr<ExponentialRandomVariable> onRv = CreateObject<ExponentialRandomVariable>();
    onRv->SetAttribute("Mean", DoubleValue(ctx->config->randomFaultOnMean));
    onRv->SetStream(ctx->config->randomFaultStream);

    Ptr<ExponentialRandomVariable> offRv = CreateObject<ExponentialRandomVariable>();
    offRv->SetAttribute("Mean", DoubleValue(ctx->config->randomFaultOffMean));
    offRv->SetStream(ctx->config->randomFaultStream + 1);

    double cursor = begin;
    while (cursor < simStop)
    {
        double offDuration = offRv->GetValue();
        if (offDuration < 0.0)
        {
            offDuration = 0.0;
        }
        cursor += offDuration;
        if (cursor >= simStop)
        {
            break;
        }

        double onDuration = onRv->GetValue();
        if (onDuration <= 0.0)
        {
            onDuration = std::numeric_limits<double>::epsilon();
        }
        double start = cursor;
        double stop = std::min(simStop, start + onDuration);
        if (stop > start)
        {
            ctx->faults.emplace_back(ctx->config->faultFrom, ctx->config->faultTo, start, stop);
        }
        cursor = stop;
    }
}

static void
ScheduleSnapshotRefresh(ForwarderContext* ctx, double period)
{
    InstallSnapshotParents(ctx);
    if (period > 0.0)
    {
        Simulator::Schedule(Seconds(period), &ScheduleSnapshotRefresh, ctx, period);
    }
}

static void
ReportStats(const ForwarderContext* ctx, const std::string& modeStr)
{
    std::cout << "RESULT mode=" << modeStr << " delivered=" << ctx->metrics.delivered
              << " ttlDrops=" << ctx->metrics.ttlDrops
              << " noRouteDrops=" << ctx->metrics.noRouteDrops
              << " blockedTx=" << ctx->metrics.blockedTransmissions
              << " queueDrops=" << ctx->metrics.queueDrops << " wasteTx=" << ctx->metrics.wasteTx
              << std::endl;
}

static uint16_t
SelectNextHop(ForwarderContext* ctx, uint16_t nodeIndex, RingHeader* header)
{
    if (nodeIndex == ctx->config->sinkId)
    {
        return nodeIndex;
    }

    auto setDirIfUnset = [&](RouteDirection dir) {
        if (header && header->GetDirection() == RouteDirection::AUTO)
        {
            header->SetDirection(dir);
        }
    };

    if (ctx->mode == ControllerMode::GLOBAL)
    {
        uint16_t next = ctx->parent[nodeIndex];
        if (header)
        {
            if (next == ClockwiseNeighbor(nodeIndex))
            {
                setDirIfUnset(RouteDirection::CW);
            }
            else if (next == CounterClockwiseNeighbor(nodeIndex))
            {
                setDirIfUnset(RouteDirection::CCW);
            }
        }
        return next;
    }

    RouteDirection dir = header ? header->GetDirection() : RouteDirection::AUTO;
    if (dir == RouteDirection::CW)
    {
        return ClockwiseNeighbor(nodeIndex);
    }
    if (dir == RouteDirection::CCW)
    {
        return CounterClockwiseNeighbor(nodeIndex);
    }

    uint16_t cw = ClockwiseNeighbor(nodeIndex);
    uint16_t ccw = CounterClockwiseNeighbor(nodeIndex);

    bool cwOk = LinkAllowed(ctx, nodeIndex, cw);
    bool ccwOk = LinkAllowed(ctx, nodeIndex, ccw);

    if (cwOk)
    {
        return cw;
    }
    if (ccwOk)
    {
        setDirIfUnset(RouteDirection::CCW);
        return ccw;
    }

    return nodeIndex;
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
    uint32_t sequence = ctx->nextSequence++;
    RingHeader header(config->sourceId, config->sinkId, config->ttl, sequence);
    RegisterPacket(ctx, sequence);
    NS_LOG_UNCOND("Source " << config->sourceId << " injecting seq=" << header.GetSequence());
    uint16_t nextHop = SelectNextHop(ctx, config->sourceId, &header);
    if (nextHop == config->sourceId)
    {
        NS_LOG_UNCOND("Source " << config->sourceId << " has no available neighbor; dropping seq="
                                << header.GetSequence());
        ctx->metrics.noRouteDrops++;
        RecordDropEvent(ctx, header.GetSequence(), "no_route", config->sourceId);
    }
    else
    {
        packet->AddHeader(header);
        SendToNeighbor(ctx, config->sourceId, nextHop, packet, header.GetSequence());
    }

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
    std::string modeStr = "global";
    std::string badArcStr;
    std::string faultModeStr = "fixed";

    CommandLine cmd(__FILE__);
    cmd.AddValue("verbose", "turn on all log components", verbose);
    cmd.AddValue("radius", "ring radius in meters", radius);
    cmd.AddValue("source", "source node index (0-5)", config.sourceId);
    cmd.AddValue("sink", "sink node index (0-5)", config.sinkId);
    cmd.AddValue("rate", "source packet rate (packets per second)", config.ratePps);
    cmd.AddValue("count", "number of application packets to send", config.packetCount);
    cmd.AddValue("payload", "application payload size in bytes", config.payloadBytes);
    cmd.AddValue("ttl", "initial TTL carried in RingHeader", config.ttl);
    cmd.AddValue("mode", "controller mode: global or local", modeStr);
    cmd.AddValue("Tinfo", "snapshot period (seconds) for global mode; 0 disables", config.snapshotPeriod);
    cmd.AddValue("simTime", "total simulation time (seconds); 0 uses auto stop", config.simTime);
    cmd.AddValue("B", "per-node buffer capacity (packets)", config.bufferCapacity);
    cmd.AddValue("faultMode", "fault scheduling mode: fixed or random", faultModeStr);
    cmd.AddValue("badArc",
                 "directed link to block during window, format i,j (optional)",
                 badArcStr);
    cmd.AddValue("badOn", "fault start time (seconds)", config.faultOn);
    cmd.AddValue("badOff", "fault end time (seconds)", config.faultOff);
    cmd.AddValue("faultOnMean",
                 "mean duration (seconds) of blocked window when faultMode=random",
                 config.randomFaultOnMean);
    cmd.AddValue("faultOffMean",
                 "mean duration (seconds) of healthy window when faultMode=random",
                 config.randomFaultOffMean);
    cmd.AddValue("faultStart", "time to begin randomized fault toggling", config.randomFaultStart);
    cmd.AddValue("faultStream",
                 "RNG stream index used for randomized fault durations",
                 config.randomFaultStream);
    cmd.Parse(argc, argv);

    if (faultModeStr == "fixed")
    {
        config.randomFaults = false;
    }
    else if (faultModeStr == "random")
    {
        config.randomFaults = true;
    }
    else
    {
        NS_ABORT_MSG("Unsupported faultMode '" << faultModeStr << "'. Use fixed or random.");
    }

    NS_ABORT_MSG_IF(config.simTime < 0.0, "simTime must be >= 0");
    NS_ABORT_MSG_IF(config.randomFaultStart < 0.0, "faultStart must be >= 0");
    NS_ABORT_MSG_IF(config.randomFaultStream < 0, "faultStream must be >= 0");

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
    NS_ABORT_MSG_IF(config.bufferCapacity == 0, "B must be >= 1");

    if (!badArcStr.empty())
    {
        uint16_t from = 0;
        uint16_t to = 0;
        char comma = 0;
        std::stringstream ss(badArcStr);
        ss >> from >> comma >> to;
        NS_ABORT_MSG_IF(ss.fail(), "badArc must be formatted as i,j");
        NS_ABORT_MSG_IF(comma != ',', "badArc must be formatted as i,j");
        NS_ABORT_MSG_IF(from >= kNumNodes || to >= kNumNodes, "badArc nodes must be within ring");
        NS_ABORT_MSG_IF(from == to, "badArc requires distinct endpoints");
        config.hasFault = true;
        config.faultFrom = from;
        config.faultTo = to;
    }

    if (config.hasFault)
    {
        if (config.randomFaults)
        {
            NS_ABORT_MSG_IF(config.randomFaultOnMean <= 0.0,
                            "faultOnMean must be greater than zero for random faults");
            NS_ABORT_MSG_IF(config.randomFaultOffMean <= 0.0,
                            "faultOffMean must be greater than zero for random faults");
        }
        else
        {
            NS_ABORT_MSG_IF(config.faultOff <= config.faultOn,
                            "badOff must be greater than badOn for fixed faults");
        }
    }
    else
    {
        NS_ABORT_MSG_IF(config.randomFaults, "faultMode=random requires --badArc");
    }

    double trafficWindow = 0.0;
    if (config.packetCount > 0 && config.ratePps > 0)
    {
        trafficWindow = static_cast<double>(config.packetCount - 1) / config.ratePps;
    }
    double defaultSimStop = kTrafficStart + trafficWindow + 5.0;
    double simStop = (config.simTime > 0.0) ? config.simTime : defaultSimStop;
    simStop = std::max(simStop, kTrafficStart + 1.0);

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
    if (modeStr == "global")
    {
        ctx.mode = ControllerMode::GLOBAL;
    }
    else if (modeStr == "local")
    {
        ctx.mode = ControllerMode::LOCAL;
    }
    else
    {
        NS_ABORT_MSG("Unsupported mode '" << modeStr << "'. Use global or local.");
    }

    if (config.hasFault)
    {
        if (config.randomFaults)
        {
            GenerateRandomFaultWindows(&ctx, config.randomFaultStart, simStop);
        }
        else
        {
            ctx.faults.emplace_back(config.faultFrom, config.faultTo, config.faultOn, config.faultOff);
        }
    }

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
        devices[i]->GetMac()->SetMcpsDataConfirmCallback(MakeBoundCallback(&DataConfirm, &ctx, i));
    }

    if (ctx.mode == ControllerMode::GLOBAL)
    {
        ScheduleSnapshotRefresh(&ctx, config.snapshotPeriod);
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

    Simulator::Stop(Seconds(simStop));
    Simulator::Run();
    Simulator::Destroy();

    ReportStats(&ctx, modeStr);

    return 0;
}
