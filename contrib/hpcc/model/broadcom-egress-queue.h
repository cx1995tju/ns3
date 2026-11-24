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

#ifndef BROADCOM_EGRESS_H
#define BROADCOM_EGRESS_H

#include "ns3/packet.h"
#include "ns3/queue.h"

#include <array>
#include <deque>

namespace ns3
{
/// Switch egress queue with strict priority for class zero and PFC-aware round robin.
class BEgressQueue : public Queue<Packet>
{
  public:
    static constexpr unsigned qCnt = 8;
    static TypeId GetTypeId();
    bool Enqueue(Ptr<Packet> packet, uint32_t priority);
    bool Enqueue(Ptr<Packet> packet) override;
    Ptr<Packet> DequeueRR(const bool paused[]);
    Ptr<Packet> Dequeue() override;
    Ptr<Packet> Remove() override;
    Ptr<const Packet> Peek() const override;
    uint32_t GetNBytes(uint32_t priority) const;
    uint32_t GetNBytesTotal() const;
    uint32_t GetLastQueue() const;

  private:
    unsigned Select(const bool paused[]) const;
    Ptr<Packet> Pop(unsigned priority, bool drop);
    std::array<std::deque<Iterator>, qCnt> m_queues;
    std::array<uint32_t, qCnt> m_bytes{};
    unsigned m_rrlast{0};
    unsigned m_qlast{0};
    double m_maxBytes;
    TracedCallback<Ptr<const Packet>, uint32_t> m_traceBeqEnqueue;
    TracedCallback<Ptr<const Packet>, uint32_t> m_traceBeqDequeue;
};
} // namespace ns3
#endif
