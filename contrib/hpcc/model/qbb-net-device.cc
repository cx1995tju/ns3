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
 *
 * Author: Yuliang Li <yuliangli@g.harvard.com>
 */

#include "ns3/qbb-net-device.h"

#include "switch-node.h"

#include "ns3/assert.h"
#include "ns3/boolean.h"
#include "ns3/cn-header.h"
#include "ns3/custom-header.h"
#include "ns3/data-rate.h"
#include "ns3/double.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/error-model.h"
#include "ns3/flow-id-tag.h"
#include "ns3/hpcc-ppp-header.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/object-vector.h"
#include "ns3/pause-header.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/pointer.h"
#include "ns3/qbb-channel.h"
#include "ns3/qbb-header.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rdma-seq-ts-header.h"
#include "ns3/simulator.h"
#include "ns3/udp-header.h"
#include "ns3/uinteger.h"

#include <iostream>
#include <stdint.h>
#include <stdio.h>

NS_LOG_COMPONENT_DEFINE("QbbNetDevice");

namespace ns3
{
NS_OBJECT_ENSURE_REGISTERED(RdmaEgressQueue);

uint32_t RdmaEgressQueue::ack_q_idx = 3;

// RdmaEgressQueue
TypeId
RdmaEgressQueue::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::RdmaEgressQueue")
            .SetParent<Object>()
            .SetGroupName("Hpcc")
            .AddConstructor<RdmaEgressQueue>()
            .AddTraceSource("RdmaEnqueue",
                            "Enqueue a packet in the RdmaEgressQueue.",
                            MakeTraceSourceAccessor(&RdmaEgressQueue::m_traceRdmaEnqueue),
                            "ns3::QbbNetDevice::QueueCallback")
            .AddTraceSource("RdmaDequeue",
                            "Dequeue a packet in the RdmaEgressQueue.",
                            MakeTraceSourceAccessor(&RdmaEgressQueue::m_traceRdmaDequeue),
                            "ns3::QbbNetDevice::QueueCallback");
    return tid;
}

RdmaEgressQueue::RdmaEgressQueue()
{
    m_rrlast = 0;
    m_qlast = 0;
    m_ackQ = CreateObject<DropTailQueue<Packet>>();
    m_ackQ->SetMaxSize(QueueSize("4294967295B")); // queue limit is on a higher level, not here
}

Ptr<Packet>
RdmaEgressQueue::DequeueQindex(int qIndex)
{
    if (qIndex == -1)
    { // high prio
        Ptr<Packet> p = m_ackQ->Dequeue();
        m_qlast = -1;
        m_traceRdmaDequeue(p, 0);
        return p;
    }
    if (qIndex >= 0)
    { // qp
        Ptr<Packet> p = m_rdmaGetNxtPkt(m_qpGrp->Get(qIndex));
        m_rrlast = qIndex;
        m_qlast = qIndex;
        m_traceRdmaDequeue(p, m_qpGrp->Get(qIndex)->m_pg);
        return p;
    }
    return 0;
}

int
RdmaEgressQueue::GetNextQindex(bool paused[])
{
    uint32_t qIndex;
    if (!paused[ack_q_idx] && m_ackQ->GetNPackets() > 0)
    {
        return -1;
    }

    // no pkt in highest priority queue, do rr for each qp
    int res = -1024;
    uint32_t fcount = m_qpGrp->GetN();
    uint32_t min_finish_id = 0xffffffff;
    for (qIndex = 1; qIndex <= fcount; qIndex++)
    {
        uint32_t idx = (qIndex + m_rrlast) % fcount;
        Ptr<RdmaQueuePair> qp = m_qpGrp->Get(idx);
        if (!paused[qp->m_pg] && qp->GetBytesLeft() > 0 && !qp->IsWinBound())
        {
            if (m_qpGrp->Get(idx)->m_nextAvail.GetTimeStep() >
                Simulator::Now().GetTimeStep()) // not available now
            {
                continue;
            }
            res = idx;
            break;
        }
        else if (qp->IsFinished())
        {
            min_finish_id = idx < min_finish_id ? idx : min_finish_id;
        }
    }

    // clear the finished qp
    if (min_finish_id < 0xffffffff)
    {
        int nxt = min_finish_id;
        auto& qps = m_qpGrp->m_qps;
        for (uint32_t i = min_finish_id + 1; i < fcount; i++)
        {
            if (!qps[i]->IsFinished())
            {
                if (static_cast<int>(i) == res) // update res to the idx after removing finished qp
                {
                    res = nxt;
                }
                qps[nxt] = qps[i];
                nxt++;
            }
        }
        qps.resize(nxt);
    }
    return res;
}

int
RdmaEgressQueue::GetLastQueue()
{
    return m_qlast;
}

uint32_t
RdmaEgressQueue::GetNBytes(uint32_t qIndex)
{
    NS_ASSERT_MSG(qIndex < m_qpGrp->GetN(),
                  "RdmaEgressQueue::GetNBytes: qIndex >= m_qpGrp->GetN()");
    return m_qpGrp->Get(qIndex)->GetBytesLeft();
}

uint32_t
RdmaEgressQueue::GetFlowCount(void)
{
    return m_qpGrp->GetN();
}

Ptr<RdmaQueuePair>
RdmaEgressQueue::GetQp(uint32_t i)
{
    return m_qpGrp->Get(i);
}

void
RdmaEgressQueue::RecoverQueue(uint32_t i)
{
    NS_ASSERT_MSG(i < m_qpGrp->GetN(), "RdmaEgressQueue::RecoverQueue: qIndex >= m_qpGrp->GetN()");
    m_qpGrp->Get(i)->snd_nxt = m_qpGrp->Get(i)->snd_una;
}

void
RdmaEgressQueue::EnqueueHighPrioQ(Ptr<Packet> p)
{
    m_traceRdmaEnqueue(p, 0);
    m_ackQ->Enqueue(p);
}

void
RdmaEgressQueue::CleanHighPrio(TracedCallback<Ptr<const Packet>, uint32_t> dropCb)
{
    while (m_ackQ->GetNPackets() > 0)
    {
        Ptr<Packet> p = m_ackQ->Dequeue();
        dropCb(p, 0);
    }
}

/******************
 * QbbNetDevice
 *****************/
NS_OBJECT_ENSURE_REGISTERED(QbbNetDevice);

TypeId
QbbNetDevice::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::QbbNetDevice")
            .SetParent<PointToPointNetDevice>()
            .SetGroupName("Hpcc")
            .AddConstructor<QbbNetDevice>()
            .AddAttribute("QbbEnabled",
                          "Enable the generation of PAUSE packet.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&QbbNetDevice::m_qbbEnabled),
                          MakeBooleanChecker())
            .AddAttribute("QcnEnabled",
                          "Enable the generation of PAUSE packet.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&QbbNetDevice::m_qcnEnabled),
                          MakeBooleanChecker())
            .AddAttribute("DynamicThreshold",
                          "Enable dynamic threshold.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&QbbNetDevice::m_dynamicth),
                          MakeBooleanChecker())
            .AddAttribute("PauseTime",
                          "Number of microseconds to pause upon congestion",
                          UintegerValue(5),
                          MakeUintegerAccessor(&QbbNetDevice::m_pausetime),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("TxBeQueue",
                          "A queue to use as the transmit queue in the device.",
                          PointerValue(),
                          MakePointerAccessor(&QbbNetDevice::m_queue),
                          MakePointerChecker<BEgressQueue>())
            .AddAttribute("RdmaEgressQueue",
                          "A queue to use as the transmit queue in the device.",
                          TypeId::ATTR_GET,
                          PointerValue(),
                          MakePointerAccessor(&QbbNetDevice::m_rdmaEQ),
                          MakePointerChecker<RdmaEgressQueue>())
            .AddTraceSource("QbbEnqueue",
                            "Enqueue a packet in the QbbNetDevice.",
                            MakeTraceSourceAccessor(&QbbNetDevice::m_traceEnqueue),
                            "ns3::QbbNetDevice::QueueCallback")
            .AddTraceSource("QbbDequeue",
                            "Dequeue a packet in the QbbNetDevice.",
                            MakeTraceSourceAccessor(&QbbNetDevice::m_traceDequeue),
                            "ns3::QbbNetDevice::QueueCallback")
            .AddTraceSource("QbbDrop",
                            "Drop a packet in the QbbNetDevice.",
                            MakeTraceSourceAccessor(&QbbNetDevice::m_traceDrop),
                            "ns3::QbbNetDevice::QueueCallback")
            .AddTraceSource("RdmaQpDequeue",
                            "A qp dequeue a packet.",
                            MakeTraceSourceAccessor(&QbbNetDevice::m_traceQpDequeue),
                            "ns3::QbbNetDevice::QpDequeueCallback")
            .AddTraceSource("QbbPfc",
                            "get a PFC packet. 0: resume, 1: pause",
                            MakeTraceSourceAccessor(&QbbNetDevice::m_tracePfc),
                            "ns3::TracedValueCallback::Uint32");

    return tid;
}

QbbNetDevice::QbbNetDevice()
{
    NS_LOG_FUNCTION(this);

    for (uint32_t i = 0; i < qCnt; i++)
    {
        m_paused[i] = false;
    }

    m_rdmaEQ = CreateObject<RdmaEgressQueue>();
}

QbbNetDevice::~QbbNetDevice()
{
    NS_LOG_FUNCTION(this);
}

void
QbbNetDevice::DoDispose()
{
    NS_LOG_FUNCTION(this);

    m_nextSend.Cancel();
    m_rdmaEQ = nullptr;
    m_queue = nullptr;
    m_rdmaReceiveCb.Nullify();
    m_rdmaPktSent.Nullify();
    m_rdmaLinkDownCb.Nullify();
    PointToPointNetDevice::DoDispose();
}

void
QbbNetDevice::DequeueAndTransmit(void)
{
    NS_LOG_FUNCTION(this);
    if (!m_linkUp)
    {
        return; // if link is down, return
    }
    if (m_txMachineState == BUSY)
    {
        return; // Quit if channel busy
    }
    Ptr<Packet> p;
    if (!m_node->GetObject<SwitchNode>())
    {
        int qIndex = m_rdmaEQ->GetNextQindex(m_paused);
        if (qIndex != -1024)
        {
            if (qIndex == -1)
            { // high prio
                p = m_rdmaEQ->DequeueQindex(qIndex);
                m_traceDequeue(p, 0);
                TransmitStart(p);
                return;
            }
            // a qp dequeue a packet
            Ptr<RdmaQueuePair> lastQp = m_rdmaEQ->GetQp(qIndex);
            p = m_rdmaEQ->DequeueQindex(qIndex);

            // transmit
            m_traceQpDequeue(p, lastQp);
            TransmitStart(p);

            // update for the next avail time
            m_rdmaPktSent(lastQp, p, m_tInterframeGap);
        }
        else
        { // no packet to send
            NS_LOG_INFO("PAUSE prohibits send at node " << m_node->GetId());
            Time t = Simulator::GetMaximumSimulationTime();
            for (uint32_t i = 0; i < m_rdmaEQ->GetFlowCount(); i++)
            {
                Ptr<RdmaQueuePair> qp = m_rdmaEQ->GetQp(i);
                if (qp->GetBytesLeft() == 0 || qp->IsWinBound() || m_paused[qp->m_pg])
                {
                    continue;
                }
                t = Min(qp->m_nextAvail, t);
            }
            if (m_nextSend.IsExpired() && t < Simulator::GetMaximumSimulationTime() &&
                t > Simulator::Now())
            {
                m_nextSend = Simulator::Schedule(t - Simulator::Now(),
                                                 &QbbNetDevice::DequeueAndTransmit,
                                                 this);
            }
        }
        return;
    }
    else
    {                                     // switch, doesn't care about qcn, just send
        p = m_queue->DequeueRR(m_paused); // this is round-robin
        if (p != nullptr)
        {
            m_snifferTrace(p);
            m_promiscSnifferTrace(p);
            FlowIdTag tag;
            uint32_t qIndex = m_queue->GetLastQueue();
            m_node->GetObject<SwitchNode>()->SwitchNotifyDequeue(m_ifIndex, qIndex, p);
            p->RemovePacketTag(tag);
            m_traceDequeue(p, qIndex);
            TransmitStart(p);
            return;
        }
    }
    return;
}

void
QbbNetDevice::Resume(unsigned qIndex)
{
    NS_LOG_FUNCTION(this << qIndex);
    NS_ASSERT_MSG(m_paused[qIndex], "Must be PAUSEd");
    m_paused[qIndex] = false;
    NS_LOG_INFO("Node " << m_node->GetId() << " dev " << m_ifIndex << " queue " << qIndex
                        << " resumed at " << Simulator::Now().GetSeconds());
    DequeueAndTransmit();
}

void
QbbNetDevice::Receive(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);
    if (!m_linkUp)
    {
        m_traceDrop(packet, 0);
        return;
    }

    if (m_receiveErrorModel && m_receiveErrorModel->IsCorrupt(packet))
    {
        //
        // If we have an error model and it indicates that it is time to lose a
        // corrupted packet, don't forward this packet up, let it go.
        //
        m_phyRxDropTrace(packet);
        return;
    }

    m_macRxTrace(packet);
    CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
    ch.getInt = 1; // parse INT header
    packet->PeekHeader(ch);
    if (ch.l3Prot == 0xFE)
    { // PFC
        if (!m_qbbEnabled)
        {
            return;
        }
        unsigned qIndex = ch.pfc.qIndex;
        NS_ABORT_MSG_IF(qIndex >= qCnt, "Invalid PFC priority");
        if (ch.pfc.time > 0)
        {
            m_tracePfc(1);
            m_paused[qIndex] = true;
        }
        else
        {
            m_tracePfc(0);
            Resume(qIndex);
        }
    }
    else
    { // non-PFC packets (data, ACK, NACK, CNP...)
        if (auto sw = m_node->GetObject<SwitchNode>())
        { // switch
            packet->AddPacketTag(FlowIdTag(m_ifIndex));
            sw->SwitchReceiveFromDevice(this, packet, ch);
        }
        else
        { // NIC
            // send to RdmaHw
            if (!m_rdmaReceiveCb.IsNull())
            {
                m_rdmaReceiveCb(packet, ch);
            }
        }
    }
    return;
}

bool
QbbNetDevice::Send(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
    // This device accepts RDMA and switch traffic through its priority-aware queues.
    m_macTxDropTrace(packet);
    return false;
}

bool
QbbNetDevice::SwitchSend(uint32_t qIndex, Ptr<Packet> packet, CustomHeader& ch)
{
    m_macTxTrace(packet);
    if (!m_linkUp || !m_queue->Enqueue(packet, qIndex))
    {
        m_traceDrop(packet, qIndex);
        return false;
    }
    m_traceEnqueue(packet, qIndex);
    DequeueAndTransmit();
    return true;
}

void
QbbNetDevice::SendPfc(uint32_t qIndex, uint32_t type)
{
    Ptr<Packet> p = Create<Packet>(0);
    PauseHeader pauseh((type == 0 ? m_pausetime : 0), m_queue->GetNBytes(qIndex), qIndex);
    p->AddHeader(pauseh);
    Ipv4Header ipv4h; // Prepare IPv4 header
    ipv4h.SetProtocol(0xFE);
    ipv4h.SetSource(m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
    ipv4h.SetDestination(Ipv4Address("255.255.255.255"));
    ipv4h.SetPayloadSize(p->GetSize());
    ipv4h.SetTtl(1);
    ipv4h.SetIdentification(m_pfcId->GetInteger(0, 65535));
    p->AddHeader(ipv4h);
    HpccPppHeader ppp;
    ppp.SetProtocol(0x0021);
    p->AddHeader(ppp);
    CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
    p->PeekHeader(ch);
    SwitchSend(0, p, ch);
}

bool
QbbNetDevice::IsQbb(void) const
{
    return true;
}

void
QbbNetDevice::NewQp(Ptr<RdmaQueuePair> qp)
{
    qp->m_nextAvail = Simulator::Now();
    DequeueAndTransmit();
}

void
QbbNetDevice::ReassignedQp(Ptr<RdmaQueuePair> qp)
{
    DequeueAndTransmit();
}

void
QbbNetDevice::TriggerTransmit(void)
{
    DequeueAndTransmit();
}

void
QbbNetDevice::SetQueue(Ptr<BEgressQueue> q)
{
    NS_LOG_FUNCTION(this << q);
    m_queue = q;
    PointToPointNetDevice::SetQueue(q);
}

Ptr<BEgressQueue>
QbbNetDevice::GetQueue()
{
    return m_queue;
}

Ptr<RdmaEgressQueue>
QbbNetDevice::GetRdmaQueue()
{
    return m_rdmaEQ;
}

void
QbbNetDevice::RdmaEnqueueHighPrioQ(Ptr<Packet> p)
{
    m_traceEnqueue(p, 0);
    m_rdmaEQ->EnqueueHighPrioQ(p);
}

void
QbbNetDevice::TakeDown()
{
    m_linkUp = false;
    m_nextSend.Cancel();

    if (!m_node->GetObject<SwitchNode>())
    {
        // clean the high prio queue
        m_rdmaEQ->CleanHighPrio(m_traceDrop);
        // notify driver/RdmaHw that this link is down
        if (!m_rdmaLinkDownCb.IsNull())
        {
            m_rdmaLinkDownCb(this);
        }
    }
    else
    { // switch
        // clean the queue
        for (uint32_t i = 0; i < qCnt; i++)
        {
            m_paused[i] = false;
        }
        while (1)
        {
            Ptr<Packet> p = m_queue->DequeueRR(m_paused);
            if (p == nullptr)
            {
                break;
            }
            auto priority = m_queue->GetLastQueue();
            m_node->GetObject<SwitchNode>()->ReleaseBuffer(m_ifIndex, priority, p);
            m_traceDrop(p, priority);
        }
    }
    m_linkChangeCallbacks();
}

void
QbbNetDevice::UpdateNextAvail(Time t)
{
    if (!m_nextSend.IsExpired() && t < TimeStep(m_nextSend.GetTs()))
    {
        Simulator::Cancel(m_nextSend);
        Time delta = t < Simulator::Now() ? Time(0) : t - Simulator::Now();
        m_nextSend = Simulator::Schedule(delta, &QbbNetDevice::DequeueAndTransmit, this);
    }
}
} // namespace ns3
