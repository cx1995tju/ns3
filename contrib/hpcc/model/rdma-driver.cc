#include "rdma-driver.h"

namespace ns3
{
NS_OBJECT_ENSURE_REGISTERED(RdmaDriver);

/***********************
 * RdmaDriver
 **********************/
TypeId
RdmaDriver::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::RdmaDriver")
                            .SetParent<Object>()
                            .SetGroupName("Hpcc")
                            .AddConstructor<RdmaDriver>()
                            .AddTraceSource("QpComplete",
                                            "A qp completes.",
                                            MakeTraceSourceAccessor(&RdmaDriver::m_traceQpComplete),
                                            "ns3::RdmaDriver::QpCompleteCallback");
    return tid;
}

RdmaDriver::RdmaDriver()
{
}

void
RdmaDriver::DoDispose()
{
    if (m_rdma)
    {
        m_rdma->Dispose();
    }
    m_rdma = nullptr;
    m_node = nullptr;
    Object::DoDispose();
}

void
RdmaDriver::Init(void)
{
    for (uint32_t i = 0; i < m_node->GetNDevices(); i++)
    {
        Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_node->GetDevice(i));
        m_rdma->m_nic.push_back(RdmaInterfaceMgr(dev));
        m_rdma->m_nic.back().qpGrp = CreateObject<RdmaQueuePairGroup>();
    }
    // RdmaHw do setup
    m_rdma->SetNode(m_node);
    m_rdma->Setup(MakeCallback(&RdmaDriver::QpComplete, this));
}

void
RdmaDriver::SetNode(Ptr<Node> node)
{
    m_node = node;
}

void
RdmaDriver::SetRdmaHw(Ptr<RdmaHw> rdma)
{
    m_rdma = rdma;
}

void
RdmaDriver::AddQueuePair(uint64_t size,
                         uint16_t pg,
                         Ipv4Address sip,
                         Ipv4Address dip,
                         uint16_t sport,
                         uint16_t dport,
                         uint32_t win,
                         uint64_t baseRtt,
                         Callback<void> notifyAppFinish)
{
    m_rdma->AddQueuePair(size, pg, sip, dip, sport, dport, win, baseRtt, notifyAppFinish);
}

void
RdmaDriver::QpComplete(Ptr<RdmaQueuePair> q)
{
    m_traceQpComplete(q);
}

} // namespace ns3
