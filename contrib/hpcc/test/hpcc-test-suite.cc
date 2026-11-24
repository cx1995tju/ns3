// SPDX-License-Identifier: GPL-2.0-only

#include "ns3/boolean.h"
#include "ns3/broadcom-egress-queue.h"
#include "ns3/double.h"
#include "ns3/enum.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/leaky-bucket.h"
#include "ns3/qbb-header.h"
#include "ns3/qbb-helper.h"
#include "ns3/rdma-client-helper.h"
#include "ns3/rdma-driver.h"
#include "ns3/rdma-seq-ts-header.h"
#include "ns3/simulator.h"
#include "ns3/switch-node.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

#include <array>

namespace ns3
{

/// Two 100 Gb/s senders share a 25 Gb/s switch port with deterministic ECN marking.
class RdmaIncastTest : public TestCase
{
  public:
    RdmaIncastTest(unsigned mode, const std::string& name)
        : TestCase(name + " incast with ECN and PFC"),
          m_mode(mode)
    {
    }

  private:
    void ObserveAck(Ptr<const Packet> packet)
    {
        CustomHeader header(CustomHeader::L2_Header | CustomHeader::L3_Header |
                            CustomHeader::L4_Header);
        packet->PeekHeader(header);
        if (header.l3Prot != 0xfc)
        {
            return;
        }
        m_ecnEcho |= (header.ack.flags & (1 << qbbHeader::FLAG_CNP)) != 0;
        if (m_mode == 3 && header.ack.ih.nhop == 1)
        {
            const auto& hop = header.ack.ih.hop[0];
            m_receivedInt |= hop.GetLineRate() == 25'000'000'000 && hop.GetQlen() > 0;
        }
    }

    void ObserveRate(Ptr<const Packet>, Ptr<RdmaQueuePair> qp)
    {
        m_byteRecovery |= qp->mlx.m_rpByteStage > 0;
        m_reducedRate |= qp->m_rate < qp->m_max_rate;
    }

    void ObservePfc(uint32_t pause)
    {
        ++m_pfc.at(pause);
    }

    void Complete(Ptr<RdmaQueuePair> qp)
    {
        NS_TEST_EXPECT_MSG_EQ(qp->snd_una,
                              qp->m_size,
                              "Final ACK must cover the partial last packet");
        ++m_finished;
    }

    void DoRun() override
    {
        auto oldMode = IntHeader::mode;
        IntHeader::mode = m_mode == 3 ? IntHeader::NORMAL : IntHeader::NONE;
        NodeContainer hosts;
        hosts.Create(3);
        auto sw = CreateObject<SwitchNode>();
        InternetStackHelper internet;
        internet.SetIpv6StackInstall(false);
        internet.Install(hosts);
        internet.Install(sw);
        sw->SetAttribute("EcnEnabled", BooleanValue(true));
        sw->SetAttribute("CcMode", UintegerValue(m_mode));
        QbbHelper links;
        links.SetChannelAttribute("Delay", TimeValue(MicroSeconds(1)));
        Ipv4AddressHelper ip;
        ip.SetBase("10.0.0.0", "255.255.255.0");
        std::array<Ipv4Address, 3> address;
        std::array<Ptr<RdmaHw>, 3> hw;
        for (unsigned i = 0; i < hosts.GetN(); ++i)
        {
            links.SetDeviceAttribute("DataRate",
                                     DataRateValue(DataRate(i == 2 ? "25Gbps" : "100Gbps")));
            auto pair = links.Install(hosts.Get(i), sw);
            address[i] = ip.Assign(pair).GetAddress(0);
            ip.NewNetwork();
            auto port = pair.Get(1)->GetIfIndex();
            sw->AddTableEntry(address[i], port);
            sw->m_mmu->ConfigEcn(port, 1, 1, 1);
            sw->m_mmu->ConfigHdrm(port, 32 * 1024);
            sw->m_mmu->pfc_a_shift[port] = 3;
            hw[i] = CreateObject<RdmaHw>();
            hw[i]->SetAttribute("CcMode", UintegerValue(m_mode));
            hw[i]->SetAttribute("RPByteReset", UintegerValue(2048));
            hw[i]->SetAttribute("L2AckInterval", UintegerValue(1));
            auto driver = CreateObject<RdmaDriver>();
            driver->SetNode(hosts.Get(i));
            driver->SetRdmaHw(hw[i]);
            hosts.Get(i)->AggregateObject(driver);
            driver->Init();
            driver->TraceConnectWithoutContext("QpComplete",
                                               MakeCallback(&RdmaIncastTest::Complete, this));
            auto nic = pair.Get(0);
            nic->TraceConnectWithoutContext("MacRx",
                                            MakeCallback(&RdmaIncastTest::ObserveAck, this));
            nic->TraceConnectWithoutContext("QbbPfc",
                                            MakeCallback(&RdmaIncastTest::ObservePfc, this));
            nic->TraceConnectWithoutContext("RdmaQpDequeue",
                                            MakeCallback(&RdmaIncastTest::ObserveRate, this));
        }
        sw->m_mmu->ConfigNPort(3);
        sw->m_mmu->ConfigBufferSize(256 * 1024);
        for (unsigned i = 0; i < 2; ++i)
        {
            hw[i]->AddTableEntry(address[2], 1);
            hw[2]->AddTableEntry(address[i], 1);
            RdmaClientHelper client(3, address[i], address[2], 10000, 100, 400001, 70000, 5000);
            client.Install(hosts.Get(i)).Start(MicroSeconds(10));
        }
        Simulator::Stop(MilliSeconds(10));
        Simulator::Run();
        NS_TEST_EXPECT_MSG_EQ(m_byteRecovery, (m_mode == 1), "Only DCQCN counts transmitted bytes");
        NS_TEST_EXPECT_MSG_EQ(m_finished, 2, "Both QPs must complete");
        NS_TEST_EXPECT_MSG_EQ(m_ecnEcho, true, "Receiver must echo a marked packet");
        NS_TEST_EXPECT_MSG_EQ(m_reducedRate,
                              true,
                              "Congestion feedback must reduce a sender's rate");
        NS_TEST_EXPECT_MSG_GT(m_pfc[1], 0, "Incast must pause an ingress priority");
        NS_TEST_EXPECT_MSG_EQ(m_pfc[0],
                              m_pfc[1],
                              "Draining the switch must resume each paused priority");
        if (m_mode == 3)
        {
            NS_TEST_EXPECT_MSG_EQ(m_receivedInt,
                                  true,
                                  "ACK must return the congested hop's rate and backlog");
        }
        Simulator::Destroy();
        IntHeader::mode = oldMode;
    }

    unsigned m_mode;
    unsigned m_finished{};
    std::array<unsigned, 2> m_pfc{};
    bool m_reducedRate{};
    bool m_byteRecovery{};
    bool m_receivedInt{};
    bool m_ecnEcho{};
};

/// Check DCQCN recovery at byte boundaries and on the actual RP timer schedule.
class DcqcnRecoveryTest : public TestCase
{
  public:
    explicit DcqcnRecoveryTest(bool useBytes)
        : TestCase(useBytes ? "DCQCN independent byte and timer recovery"
                            : "DCQCN legacy timer-only recovery"),
          m_useBytes(useBytes)
    {
    }

  private:
    void DoRun() override
    {
        auto hw = CreateObject<RdmaHw>();
        hw->SetAttribute("CcMode", UintegerValue(1));
        hw->SetAttribute("RPByteReset", UintegerValue(m_useBytes ? 1000 : 0));
        hw->SetAttribute("RPTimer", DoubleValue(10));
        hw->SetAttribute("FastRecoveryTimes", UintegerValue(2));
        hw->SetAttribute("RateAI", DataRateValue(DataRate("1Gb/s")));
        hw->SetAttribute("RateHAI", DataRateValue(DataRate("4Gb/s")));
        auto dev = CreateObject<QbbNetDevice>();
        dev->SetDataRate(DataRate("100Gb/s"));
        hw->m_nic.emplace_back(dev);
        auto qp =
            CreateObject<RdmaQueuePair>(3, Ipv4Address("10.0.0.1"), Ipv4Address("10.0.0.2"), 1, 2);
        hw->AddTableEntry(qp->dip, 0);
        qp->m_rate = DataRate("8Gb/s");
        auto send = [&](uint32_t size) { hw->PktSent(qp, Create<Packet>(size), Time(0)); };
        auto runUntil = [](unsigned us) {
            Simulator::Stop(MicroSeconds(us) - Simulator::Now());
            Simulator::Run();
        };

        send(1500);
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_rpBytes,
                              0,
                              "Line-rate startup does not accrue byte credit");
        hw->cnp_received_mlx(qp);
        runUntil(5); // First rate decrease at 4 us + 1 ns.
        send(999);
        NS_TEST_EXPECT_MSG_EQ(qp->m_rate, DataRate("4Gb/s"), "Below the byte threshold");
        send(1);
        NS_TEST_EXPECT_MSG_EQ(qp->m_rate,
                              DataRate(m_useBytes ? "6Gb/s" : "4Gb/s"),
                              "First byte event performs fast recovery");
        send(2123);
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_rpByteStage,
                              (m_useBytes ? 3 : 0),
                              "One packet can cross more than one threshold");
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_targetRate,
                              DataRate(m_useBytes ? "10Gb/s" : "8Gb/s"),
                              "Only the byte stage has reached the additive threshold");

        runUntil(25); // Timer has expired twice irrespective of the byte events.
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_rpTimeStage, 2, "Byte events preserve the timer deadline");
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_rpBytes,
                              (m_useBytes ? 123 : 0),
                              "Timer events preserve partial byte credit");
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_targetRate,
                              DataRate(m_useBytes ? "12Gb/s" : "8Gb/s"),
                              "At the dual-counter boundary recovery is still additive");
        runUntil(35);
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_targetRate,
                              DataRate(m_useBytes ? "16Gb/s" : "9Gb/s"),
                              "Both stages above threshold enable hyper increase");
        runUntil(45);
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_targetRate,
                              DataRate(m_useBytes ? "20Gb/s" : "13Gb/s"),
                              "Timer-only recovery retains its original hyper increase step");
        hw->cnp_received_mlx(qp);
        runUntil(55); // The decrease at 48 us replaces the old timer due at 54 us.
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_rpTimeStage, 0, "Rate decrease restarts the timer");
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_rpByteStage, 0, "Rate decrease clears the byte stage");
        send(877);
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_rpByteStage, 0, "Old partial credit was discarded");
        NS_TEST_EXPECT_MSG_EQ(qp->mlx.m_rpBytes, (m_useBytes ? 877 : 0), "New recovery interval");
        Simulator::Destroy();
    }

    bool m_useBytes; ///< Enable the byte threshold or exercise the legacy timer-only path
};

class HpccQueueTest : public TestCase
{
  public:
    HpccQueueTest()
        : TestCase("Priority scheduling, pause and queue accounting")
    {
    }

  private:
    void DoRun() override
    {
        auto queue = CreateObject<BEgressQueue>();
        bool paused[BEgressQueue::qCnt]{};
        auto blocked = Create<Packet>(23);
        auto eligible = Create<Packet>(37);
        auto control = Create<Packet>(9);
        queue->Enqueue(blocked, 1);
        queue->Enqueue(eligible, 2);
        queue->Enqueue(control, 0);
        paused[1] = true;
        NS_TEST_EXPECT_MSG_EQ(queue->DequeueRR(paused),
                              control,
                              "Control frames have strict priority");
        NS_TEST_EXPECT_MSG_EQ(queue->DequeueRR(paused),
                              eligible,
                              "Round robin must skip a paused priority");
        NS_TEST_EXPECT_MSG_EQ(queue->DequeueRR(paused), nullptr, "Paused data must wait");
        NS_TEST_EXPECT_MSG_EQ(queue->GetNBytesTotal(), 23, "Only the blocked frame remains");
        paused[1] = false;
        NS_TEST_EXPECT_MSG_EQ(queue->DequeueRR(paused), blocked, "Resumed data must drain");
        NS_TEST_EXPECT_MSG_EQ(queue->GetNBytes(1), 0, "Priority occupancy after dequeue");
        auto bucket = CreateObject<LeakyBucket>();
        bucket->SetAttribute("Mode", EnumValue(LeakyBucket::BYTES));
        bucket->SetAttribute("MaxBytes", UintegerValue(60));
        NS_TEST_EXPECT_MSG_EQ(bucket->Enqueue(blocked), true, "Under byte limit");
        NS_TEST_EXPECT_MSG_EQ(bucket->Enqueue(eligible),
                              false,
                              "Original bucket uses a strict byte limit");
        NS_TEST_EXPECT_MSG_EQ(bucket->GetTotalDroppedPackets(), 1, "Queue must count overflow");
        NS_TEST_EXPECT_MSG_EQ(bucket->Dequeue(), blocked, "Bucket preserves FIFO order");
    }
};

class HpccTelemetryTest : public TestCase
{
  public:
    HpccTelemetryTest()
        : TestCase("PINT wire length and HPCC hop-counter rollover")
    {
    }

  private:
    void DoRun() override
    {
        auto oldMode = IntHeader::mode;
        auto oldBytes = IntHeader::pint_bytes;
        IntHeader::mode = IntHeader::PINT;
        for (int width : {1, 2})
        {
            IntHeader::pint_bytes = width;
            uint16_t value = width == 1 ? 0x45 : 0x1234;
            RdmaSeqTsHeader seq;
            seq.SetPG(3);
            seq.SetSeq(9999);
            seq.ih.SetPower(value);
            auto packet = Create<Packet>(37);
            packet->AddHeader(seq);
            NS_TEST_EXPECT_MSG_EQ(packet->GetSize(), 43u + width, "PINT field length");
            RdmaSeqTsHeader decoded;
            packet->RemoveHeader(decoded);
            NS_TEST_EXPECT_MSG_EQ(decoded.ih.GetPower(), value, "PINT feedback");
            NS_TEST_EXPECT_MSG_EQ(decoded.GetSeq(), 9999, "RDMA sequence");
            NS_TEST_EXPECT_MSG_EQ(packet->GetSize(), 37, "Payload survives parsing");
        }
        IntHeader::mode = oldMode;
        IntHeader::pint_bytes = oldBytes;
        IntHop previous, current;
        uint64_t byteUnit = IntHop::multi * IntHop::byteUnit;
        previous.Set((1ULL << 24) - 1, ((1ULL << 20) - 1) * byteUnit, 0, 100'000'000'000);
        current.Set(1, byteUnit, 0, 100'000'000'000);
        NS_TEST_EXPECT_MSG_EQ(current.GetBytesDelta(previous),
                              2 * byteUnit,
                              "20-bit byte counter wraps");
        NS_TEST_EXPECT_MSG_EQ(current.GetTimeDelta(previous), 2, "24-bit timestamp wraps");
    }
};

class HpccTestSuite : public TestSuite
{
  public:
    HpccTestSuite()
        : TestSuite("hpcc", Type::SYSTEM)
    {
        AddTestCase(new DcqcnRecoveryTest(true), TestCase::Duration::QUICK);
        AddTestCase(new DcqcnRecoveryTest(false), TestCase::Duration::QUICK);
        AddTestCase(new HpccQueueTest, TestCase::Duration::QUICK);
        AddTestCase(new HpccTelemetryTest, TestCase::Duration::QUICK);
        AddTestCase(new RdmaIncastTest(1, "DCQCN"), TestCase::Duration::QUICK);
        AddTestCase(new RdmaIncastTest(3, "HPCC"), TestCase::Duration::QUICK);
        AddTestCase(new RdmaIncastTest(8, "DCTCP"), TestCase::Duration::QUICK);
    }
};

static HpccTestSuite g_hpccTestSuite;
} // namespace ns3
