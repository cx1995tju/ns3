#include "switch-node.h"

#include "qbb-net-device.h"
#include "rdma-seq-ts-header.h"

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/flow-id-tag.h"
#include "ns3/hpcc-ppp-header.h"
#include "ns3/int-header.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4.h"
#include "ns3/packet.h"
#include "ns3/pause-header.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

#include <bit>
#include <cmath>

namespace ns3
{
NS_OBJECT_ENSURE_REGISTERED(SwitchNode);

TypeId
SwitchNode::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::SwitchNode")
                            .SetParent<Node>()
                            .SetGroupName("Hpcc")
                            .AddConstructor<SwitchNode>()
                            .AddAttribute("EcnEnabled",
                                          "Enable ECN marking.",
                                          BooleanValue(false),
                                          MakeBooleanAccessor(&SwitchNode::m_ecnEnabled),
                                          MakeBooleanChecker())
                            .AddAttribute("CcMode",
                                          "CC mode.",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&SwitchNode::m_ccMode),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("AckHighPrio",
                                          "Set high priority for ACK/NACK or not",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&SwitchNode::m_ackHighPrio),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("MaxRtt",
                                          "Max Rtt of the network",
                                          UintegerValue(9000),
                                          MakeUintegerAccessor(&SwitchNode::m_maxRtt),
                                          MakeUintegerChecker<uint32_t>());
    return tid;
}

SwitchNode::SwitchNode()
{
    m_ecmpSeed = GetId();

    m_mmu = CreateObject<SwitchMmu>();
    for (uint32_t i = 0; i < pCnt; i++)
    {
        m_txBytes[i] = 0;
    }
    for (uint32_t i = 0; i < pCnt; i++)
    {
        m_lastPktSize[i] = m_lastPktTs[i] = 0;
    }
    for (uint32_t i = 0; i < pCnt; i++)
    {
        m_u[i] = 0;
    }
}

int
SwitchNode::GetOutDev(Ptr<const Packet> p, CustomHeader& ch)
{
    // look up entries
    auto entry = m_rtTable.find(ch.dip);

    // no matching entry
    if (entry == m_rtTable.end())
    {
        return -1;
    }

    // entry found
    auto& nexthops = entry->second;

    // pick one next hop based on hash
    std::array<uint32_t, 3> words{};
    words[0] = ch.sip;
    words[1] = ch.dip;
    if (ch.l3Prot == 0x6)
    {
        words[2] = ch.tcp.sport | ((uint32_t)ch.tcp.dport << 16);
    }
    else if (ch.l3Prot == 0x11)
    {
        words[2] = ch.udp.sport | ((uint32_t)ch.udp.dport << 16);
    }
    else if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD)
    {
        words[2] = ch.ack.sport | ((uint32_t)ch.ack.dport << 16);
    }

    uint32_t idx = EcmpHash(words, m_ecmpSeed) % nexthops.size();
    return nexthops[idx];
}

void
SwitchNode::CheckAndSendPfc(uint32_t inDev, uint32_t qIndex)
{
    Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(GetDevice(inDev));
    if (m_mmu->CheckShouldPause(inDev, qIndex))
    {
        device->SendPfc(qIndex, 0);
        m_mmu->SetPause(inDev, qIndex);
    }
}

void
SwitchNode::CheckAndSendResume(uint32_t inDev, uint32_t qIndex)
{
    Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(GetDevice(inDev));
    if (m_mmu->CheckShouldResume(inDev, qIndex))
    {
        device->SendPfc(qIndex, 1);
        m_mmu->SetResume(inDev, qIndex);
    }
}

void
SwitchNode::SendToDev(Ptr<Packet> p, CustomHeader& ch)
{
    int idx = GetOutDev(p, ch);
    if (idx >= 0)
    {
        NS_ASSERT_MSG(GetDevice(idx)->IsLinkUp(),
                      "The routing table look up should return link that is up");

        // determine the qIndex
        uint32_t qIndex;
        if (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE ||
            (m_ackHighPrio && (ch.l3Prot == 0xFD || ch.l3Prot == 0xFC)))
        { // QCN or PFC or NACK, go highest priority
            qIndex = 0;
        }
        else
        {
            qIndex = (ch.l3Prot == 0x06
                          ? 1
                          : (ch.l3Prot == 0x11 ? ch.udp.pg : ch.ack.pg)); // if TCP, put to queue 1
        }

        // admission control
        FlowIdTag t;
        p->PeekPacketTag(t);
        uint32_t inDev = t.GetFlowId();
        NS_ABORT_MSG_IF(qIndex >= qCnt || inDev >= pCnt || static_cast<uint32_t>(idx) >= pCnt,
                        "Invalid switch port or priority");
        if (qIndex != 0)
        { // not highest priority
            if (m_mmu->CheckIngressAdmission(inDev, qIndex, p->GetSize()) &&
                m_mmu->CheckEgressAdmission(idx, qIndex, p->GetSize()))
            { // Admission control
                m_mmu->UpdateIngressAdmission(inDev, qIndex, p->GetSize());
                m_mmu->UpdateEgressAdmission(idx, qIndex, p->GetSize());
            }
            else
            {
                return; // Drop
            }
            CheckAndSendPfc(inDev, qIndex);
        }
        if (!DynamicCast<QbbNetDevice>(GetDevice(idx))->SwitchSend(qIndex, p, ch))
        {
            ReleaseBuffer(idx, qIndex, p);
        }
    }
    else
    {
        return; // Drop
    }
}

uint32_t
SwitchNode::EcmpHash(const std::array<uint32_t, 3>& words, uint32_t seed)
{
    uint32_t h = seed;
    for (uint32_t k : words)
    {
        k *= 0xcc9e2d51;
        k = std::rotl(k, 15);
        k *= 0x1b873593;
        h ^= k;
        h = std::rotl(h, 13) * 5 + 0xe6546b64;
    }
    h ^= words.size() * sizeof(uint32_t);
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

void
SwitchNode::SetEcmpSeed(uint32_t seed)
{
    m_ecmpSeed = seed;
}

void
SwitchNode::AddTableEntry(Ipv4Address& dstAddr, uint32_t intf_idx)
{
    uint32_t dip = dstAddr.Get();
    m_rtTable[dip].push_back(intf_idx);
}

void
SwitchNode::ClearTable()
{
    m_rtTable.clear();
}

// This function can only be called in switch mode
bool
SwitchNode::SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader& ch)
{
    SendToDev(packet, ch);
    return true;
}

void
SwitchNode::ReleaseBuffer(uint32_t outPort, uint32_t priority, Ptr<const Packet> p)
{
    if (priority == 0)
    {
        return;
    }
    FlowIdTag ingress;
    bool tagged = p->PeekPacketTag(ingress);
    NS_ABORT_MSG_IF(!tagged, "Forwarded packet has no ingress port");
    auto inPort = ingress.GetFlowId();
    m_mmu->RemoveFromIngressAdmission(inPort, priority, p->GetSize());
    m_mmu->RemoveFromEgressAdmission(outPort, priority, p->GetSize());
    CheckAndSendResume(inPort, priority);
}

void
SwitchNode::SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p)
{
    ReleaseBuffer(ifIndex, qIndex, p);
    if (qIndex != 0)
    {
        if (m_ecnEnabled)
        {
            bool egressCongested = m_mmu->ShouldSendCN(ifIndex, qIndex);
            if (egressCongested)
            {
                HpccPppHeader ppp;
                Ipv4Header h;
                p->RemoveHeader(ppp);
                p->RemoveHeader(h);
                h.SetEcn((Ipv4Header::EcnType)0x03);
                p->AddHeader(h);
                p->AddHeader(ppp);
            }
        }
    }
    if (m_ccMode == 3 || m_ccMode == 10)
    {
        HpccPppHeader ppp;
        Ipv4Header ip;
        p->RemoveHeader(ppp);
        p->RemoveHeader(ip);
        if (ip.GetProtocol() == 0x11)
        {
            UdpHeader udp;
            RdmaSeqTsHeader seq;
            p->RemoveHeader(udp);
            p->RemoveHeader(seq);
            auto& ih = seq.ih;
            Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(GetDevice(ifIndex));
            if (m_ccMode == 3)
            { // HPCC
                ih.PushHop(Simulator::Now().GetTimeStep(),
                           m_txBytes[ifIndex],
                           dev->GetQueue()->GetNBytesTotal(),
                           dev->GetDataRate().GetBitRate());
            }
            else if (m_ccMode == 10)
            { // HPCC-PINT
                uint64_t t = Simulator::Now().GetTimeStep();
                uint64_t dt = t - m_lastPktTs[ifIndex];
                if (dt > m_maxRtt)
                {
                    dt = m_maxRtt;
                }
                uint64_t B = dev->GetDataRate().GetBitRate() / 8; // Bps
                uint64_t qlen = dev->GetQueue()->GetNBytesTotal();
                double newU;

                /**************************
                 * approximate calc
                 *************************/
                int b = 20, m = 16, l = 20; // see log2apprx's paremeters
                int sft = logres_shift(b, l);
                double fct = 1 << sft;               // (multiplication factor corresponding to sft)
                double log_T = log2(m_maxRtt) * fct; // log2(T)*fct
                double log_B = log2(B) * fct;        // log2(B)*fct
                double log_1e9 = log2(1e9) * fct;    // log2(1e9)*fct
                double qterm = 0;
                double byteTerm = 0;
                double uTerm = 0;
                if (dt && (qlen >> 8) > 0)
                {
                    int log_dt = log2apprx(dt, b, m, l);          // ~log2(dt)*fct
                    int log_qlen = log2apprx(qlen >> 8, b, m, l); // ~log2(qlen / 256)*fct
                    qterm = pow(2, (log_dt + log_qlen + log_1e9 - log_B - 2 * log_T) / fct) * 256;
                    // 2^((log2(dt)*fct+log2(qlen/256)*fct+log2(1e9)*fct-log2(B)*fct-2*log2(T)*fct)/fct)*256
                    // ~= dt*qlen*1e9/(B*T^2)
                }
                if (m_lastPktSize[ifIndex] > 0)
                {
                    int byte = m_lastPktSize[ifIndex];
                    int log_byte = log2apprx(byte, b, m, l);
                    byteTerm = pow(2, (log_byte + log_1e9 - log_B - log_T) / fct);
                    // 2^((log2(byte)*fct+log2(1e9)*fct-log2(B)*fct-log2(T)*fct)/fct) ~= byte*1e9 /
                    // (B*T)
                }
                if (m_maxRtt > dt && std::round(m_u[ifIndex] * 8192) > 0)
                {
                    int log_T_dt = log2apprx(m_maxRtt - dt, b, m, l); // ~log2(T-dt)*fct
                    int log_u =
                        log2apprx(int(round(m_u[ifIndex] * 8192)), b, m, l); // ~log2(u*512)*fct
                    uTerm = pow(2, (log_T_dt + log_u - log_T) / fct) / 8192;
                    // 2^((log2(T-dt)*fct+log2(u*512)*fct-log2(T)*fct)/fct)/512 = (T-dt)*u/T
                }
                newU = qterm + byteTerm + uTerm;

                /************************
                 * update PINT header
                 ***********************/
                uint16_t power = Pint::encode_u(newU);
                if (power > ih.GetPower())
                {
                    ih.SetPower(power);
                }

                m_u[ifIndex] = newU;
            }
            p->AddHeader(seq);
            p->AddHeader(udp);
        }
        p->AddHeader(ip);
        p->AddHeader(ppp);
    }
    m_txBytes[ifIndex] += p->GetSize();
    m_lastPktSize[ifIndex] = p->GetSize();
    m_lastPktTs[ifIndex] = Simulator::Now().GetTimeStep();
}

int
SwitchNode::logres_shift(int b, int l)
{
    static int data[] = {0, 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
                         5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
    return l - data[b];
}

int
SwitchNode::log2apprx(int x, int b, int m, int l)
{
    int x0 = x;
    int msb = int(log2(x)) + 1;
    if (msb > m)
    {
        x = (x >> (msb - m) << (msb - m));
        int mask = (1 << (msb - m)) - 1;
        if ((x0 & mask) > (rand() & mask))
        {
            x += 1 << (msb - m);
        }
    }
    return int(log2(x) * (1 << logres_shift(b, l)));
}

} /* namespace ns3 */
