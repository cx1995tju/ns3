/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */

#include "rdma-seq-ts-header.h"

#include "ns3/assert.h"
#include "ns3/header.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

NS_LOG_COMPONENT_DEFINE("RdmaSeqTsHeader");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(RdmaSeqTsHeader);

RdmaSeqTsHeader::RdmaSeqTsHeader()
    : m_seq(0),
      m_pg(0)
{
    if (IntHeader::mode == 1)
    {
        ih.ts = Simulator::Now().GetTimeStep();
    }
}

void
RdmaSeqTsHeader::SetSeq(uint32_t seq)
{
    m_seq = seq;
}

uint32_t
RdmaSeqTsHeader::GetSeq(void) const
{
    return m_seq;
}

void
RdmaSeqTsHeader::SetPG(uint16_t pg)
{
    m_pg = pg;
}

uint16_t
RdmaSeqTsHeader::GetPG(void) const
{
    return m_pg;
}

Time
RdmaSeqTsHeader::GetTs(void) const
{
    NS_ASSERT_MSG(IntHeader::mode == 1, "RdmaSeqTsHeader cannot GetTs when IntHeader::mode != 1");
    return TimeStep(ih.ts);
}

TypeId
RdmaSeqTsHeader::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::RdmaSeqTsHeader")
                            .SetParent<Header>()
                            .SetGroupName("Hpcc")
                            .AddConstructor<RdmaSeqTsHeader>();
    return tid;
}

TypeId
RdmaSeqTsHeader::GetInstanceTypeId(void) const
{
    return GetTypeId();
}

void
RdmaSeqTsHeader::Print(std::ostream& os) const
{
    // os << "(seq=" << m_seq << " time=" << TimeStep (m_ts).GetSeconds () << ")";
    // os << m_seq << " " << TimeStep (m_ts).GetSeconds () << " " << m_pg;
    os << m_seq << " " << m_pg;
}

uint32_t
RdmaSeqTsHeader::GetSerializedSize(void) const
{
    return GetHeaderSize();
}

uint32_t
RdmaSeqTsHeader::GetHeaderSize(void)
{
    return 6 + IntHeader::GetStaticSize();
}

void
RdmaSeqTsHeader::Serialize(Buffer::Iterator start) const
{
    Buffer::Iterator i = start;
    i.WriteHtonU32(m_seq);
    i.WriteHtonU16(m_pg);

    // write IntHeader
    ih.Serialize(i);
}

uint32_t
RdmaSeqTsHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    m_seq = i.ReadNtohU32();
    m_pg = i.ReadNtohU16();

    // read IntHeader
    ih.Deserialize(i);
    return GetSerializedSize();
}

} // namespace ns3
