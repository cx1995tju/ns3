/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation;
//
// Author: Baltzis Petros<mpaltzis@ceid.upatras.gr>

#include "leaky-bucket.h"

#include "ns3/address.h"
#include "ns3/attribute.h"
#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/enum.h"
#include "ns3/ipv4-header.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/timer.h"
#include "ns3/uinteger.h"

using namespace std;

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(LeakyBucket);

TypeId
LeakyBucket::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::LeakyBucket")
            .SetParent<Queue<Packet>>()
            .SetGroupName("Hpcc")
            .AddConstructor<LeakyBucket>()
            .AddAttribute("Ipv4Shape",
                          "The address of node to shape",
                          Ipv4AddressValue(),
                          MakeIpv4AddressAccessor(&LeakyBucket::m_peer),
                          MakeIpv4AddressChecker())
            .AddAttribute("DataRate",
                          "The data rate of shaper.",
                          DataRateValue(DataRate("500kb/s")),
                          MakeDataRateAccessor(&LeakyBucket::m_cbrRate),
                          MakeDataRateChecker())
            .AddAttribute("Mode",
                          "Whether to use Bytes (see MaxBytes) or Packets (see MaxPackets) as the "
                          "maximum queue size metric.",
                          EnumValue(PACKETS),
                          MakeEnumAccessor<Mode>(&LeakyBucket::SetMode, &LeakyBucket::GetMode),
                          MakeEnumChecker(BYTES, "Bytes", PACKETS, "Packets"))
            .AddAttribute("MaxPackets",
                          "The maximum number of packets accepted by this Bucket.",
                          UintegerValue(100),
                          MakeUintegerAccessor(&LeakyBucket::m_maxPackets),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("MaxBytes",
                          "The maximum number of bytes accepted by this Bucket.",
                          UintegerValue(100 * 65535),
                          MakeUintegerAccessor(&LeakyBucket::m_maxBytes),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("DSCP",
                          "The value of dscp.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&LeakyBucket::m_DSCP),
                          MakeUintegerChecker<uint8_t>());
    return tid;
}

LeakyBucket::LeakyBucket()
{
    // NS_LOG_FUNCTION(this);
}

LeakyBucket::~LeakyBucket()
{
    // NS_LOG_FUNCTION(this);
}

void
LeakyBucket::SetMode(enum Mode mode)
{
    // NS_LOG_FUNCTION (mode);
    m_mode = mode;
}

Ipv4Address
LeakyBucket::GetIpv4Shape(void)
{
    return m_peer;
}

DataRate
LeakyBucket::GetCbrRate(void)
{
    return m_cbrRate;
}

LeakyBucket::Mode
LeakyBucket::GetMode() const
{
    return m_mode;
}

bool
LeakyBucket::Enqueue(Ptr<Packet> p)
{
    if ((m_mode == PACKETS && GetNPackets() >= m_maxPackets) ||
        (m_mode == BYTES && static_cast<uint64_t>(GetNBytes()) + p->GetSize() >= m_maxBytes))
    {
        DropBeforeEnqueue(p);
        return false;
    }
    return DoEnqueue(GetContainer().end(), p);
}

Ptr<Packet>
LeakyBucket::Dequeue()
{
    return DoDequeue(GetContainer().begin());
}

Ptr<Packet>
LeakyBucket::Remove()
{
    return DoRemove(GetContainer().begin());
}

Ptr<const Packet>
LeakyBucket::Peek() const
{
    return DoPeek(GetContainer().begin());
}
} // namespace ns3
