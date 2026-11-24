/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007, 2008 University of Washington
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

#include "qbb-channel.h"

#include "qbb-net-device.h"

#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/trace-source-accessor.h"

#include <fstream>
#include <iostream>

NS_LOG_COMPONENT_DEFINE("QbbChannel");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(QbbChannel);

TypeId
QbbChannel::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::QbbChannel")
                            .SetParent<PointToPointChannel>()
                            .SetGroupName("Hpcc")
                            .AddConstructor<QbbChannel>()
                            .AddTraceSource("TxRxQbb",
                                            "Trace source indicating transmission of packet from "
                                            "the QbbChannel, used by the Animation interface.",
                                            MakeTraceSourceAccessor(&QbbChannel::m_txrxQbb),
                                            "ns3::PointToPointChannel::TxRxAnimationCallback");
    return tid;
}

QbbChannel::QbbChannel() = default;

Ptr<QbbNetDevice>
QbbChannel::GetQbbDevice(std::size_t i) const
{
    return DynamicCast<QbbNetDevice>(GetDevice(i));
}

bool
QbbChannel::TransmitStart(Ptr<const Packet> p, Ptr<PointToPointNetDevice> src, Time txTime)
{
    bool sent = PointToPointChannel::TransmitStart(p, src, txTime);
    m_txrxQbb(p, src, GetDestination(src == GetSource(0) ? 0 : 1), txTime, txTime + GetDelay());
    return sent;
}
} // namespace ns3
