/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2006 Georgia Tech Research Corporation, INRIA
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
 */
#include "broadcom-egress-queue.h"

#include "ns3/double.h"
#include "ns3/enum.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

#include <iostream>
#include <stdio.h>

NS_LOG_COMPONENT_DEFINE("BEgressQueue");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(BEgressQueue);

TypeId
BEgressQueue::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::BEgressQueue")
            .SetParent<Queue<Packet>>()
            .SetGroupName("Hpcc")
            .AddConstructor<BEgressQueue>()
            .AddAttribute("MaxBytes",
                          "The maximum number of bytes accepted by this BEgressQueue.",
                          DoubleValue(1000.0 * 1024 * 1024),
                          MakeDoubleAccessor(&BEgressQueue::m_maxBytes),
                          MakeDoubleChecker<double>())
            .AddTraceSource("BeqEnqueue",
                            "Enqueue a packet in the BEgressQueue. Multiple queue",
                            MakeTraceSourceAccessor(&BEgressQueue::m_traceBeqEnqueue),
                            "ns3::QbbNetDevice::QueueCallback")
            .AddTraceSource("BeqDequeue",
                            "Dequeue a packet in the BEgressQueue. Multiple queue",
                            MakeTraceSourceAccessor(&BEgressQueue::m_traceBeqDequeue),
                            "ns3::QbbNetDevice::QueueCallback");

    return tid;
}

bool
BEgressQueue::Enqueue(Ptr<Packet> p, uint32_t priority)
{
    NS_ABORT_MSG_IF(priority >= qCnt, "Invalid QBB priority " << priority);
    if (static_cast<uint64_t>(QueueBase::GetNBytes()) + p->GetSize() >= m_maxBytes)
    {
        DropBeforeEnqueue(p);
        return false;
    }
    Iterator it;
    if (!DoEnqueue(GetContainer().end(), p, it))
    {
        return false;
    }
    m_queues[priority].push_back(it);
    m_bytes[priority] += p->GetSize();
    m_traceBeqEnqueue(p, priority);
    return true;
}

bool
BEgressQueue::Enqueue(Ptr<Packet> p)
{
    return Enqueue(p, 0);
}

unsigned
BEgressQueue::Select(const bool paused[]) const
{
    if (!m_queues[0].empty())
    {
        return 0;
    }
    for (unsigned i = 1; i <= qCnt; ++i)
    {
        unsigned q = (m_rrlast + i) % qCnt;
        if ((!paused || !paused[q]) && !m_queues[q].empty())
        {
            return q;
        }
    }
    return qCnt;
}

Ptr<Packet>
BEgressQueue::Pop(unsigned q, bool drop)
{
    if (q == qCnt)
    {
        return nullptr;
    }
    auto it = m_queues[q].front();
    m_queues[q].pop_front();
    m_bytes[q] -= (*it)->GetSize();
    m_qlast = q;
    if (q != 0)
    {
        m_rrlast = q;
    }
    auto p = drop ? DoRemove(it) : DoDequeue(it);
    m_traceBeqDequeue(p, q);
    return p;
}

Ptr<Packet>
BEgressQueue::DequeueRR(const bool paused[])
{
    return Pop(Select(paused), false);
}

Ptr<Packet>
BEgressQueue::Dequeue()
{
    return DequeueRR(nullptr);
}

Ptr<Packet>
BEgressQueue::Remove()
{
    return Pop(Select(nullptr), true);
}

Ptr<const Packet>
BEgressQueue::Peek() const
{
    unsigned q = Select(nullptr);
    return q == qCnt ? nullptr : *m_queues[q].front();
}

uint32_t
BEgressQueue::GetNBytes(uint32_t q) const
{
    return m_bytes.at(q);
}

uint32_t
BEgressQueue::GetNBytesTotal() const
{
    return QueueBase::GetNBytes();
}

uint32_t
BEgressQueue::GetLastQueue() const
{
    return m_qlast;
}
} // namespace ns3
