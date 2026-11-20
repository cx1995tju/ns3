/*
 * Copyright (c) 2015 Natale Patriciello <natale.patriciello@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */
#ifndef TCPCONGESTIONOPS_H
#define TCPCONGESTIONOPS_H

#include "tcp-rate-ops.h"
#include "tcp-socket-state.h"

namespace ns3
{

/**
 * @ingroup tcp
 * @defgroup congestionOps Congestion Control Algorithms.
 *
 * The various congestion control algorithms, also known as "TCP flavors".
 */

/**
 * @ingroup congestionOps
 *
 * @brief Congestion control abstract class
 *
 * The design is inspired by what Linux v4.0 does (but it has been
 * in place for years). The congestion control is split from the main
 * socket code, and it is a pluggable component. An interface has been defined;
 * variables are maintained in the TcpSocketState class, while subclasses of
 * TcpCongestionOps operate over an instance of that class.
 *
 * Only three methods have been implemented right now; however, Linux has many others,
 * which can be added later in ns-3.
 *
 * @see IncreaseWindow
 * @see PktsAcked
 *
 *  接口                                         什么时候用、做什么
 * ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 *  PktsAcked(tcb, segmentsAcked, rtt)             socket 处理 ACK 时，将确认的段数和 RTT 信息交给算法。算法可以更新测量值，也可以像 TCP-LP 那样主动减窗
 * ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
 *  IncreaseWindow(tcb, segmentsAcked)             当 TCP 根据当前状态允许增窗时调用。以 NewReno 为例，它根据 cwnd 与 ssthresh 的关系执行慢启动或拥塞避免
 * ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
 *  GetSsThresh(tcb, bytesInFlight)                发生丢包等拥塞事件时计算新的慢启动门限。这里的 tcb 是只读的，算法返回门限值，由 socket 写入 m_ssThresh
 * ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
 *  CongestionStateSet(tcb, newState)              通知算法拥塞状态即将变化，例如进入 CA_RECOVERY。随后由 socket 更新 m_congState
 * ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
 *  CwndEvent(tcb, event)                          通知算法某个具体事件，例如开始发送、收到 ECN 标记或发生超时丢包
 * ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
 *  HasCongControl() / CongControl(tcb, rc, rs)    供 BBR 等算法使用。前者声明算法支持这个入口，后者利用连接的交付速率信息和本次采样调整 cwnd、发送节奏（pacing rate）
 * ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
 *  Init(tcb) / SetRateOps(...)                    初始化连接相关的算法配置，或向需要速率测量的算法提供 TcpRateOps 对象
 * ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
 *  GetName() / Fork()                             返回算法名称，或在复制 socket 时复制算法对象，使各连接可以维护自己的算法状态
 */
class TcpCongestionOps : public Object
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    TcpCongestionOps();

    /**
     * @brief Copy constructor.
     * @param other object to copy.
     */
    TcpCongestionOps(const TcpCongestionOps& other);

    ~TcpCongestionOps() override;

    /**
     * @brief Get the name of the congestion control algorithm
     *
     * @return A string identifying the name
     */
    virtual std::string GetName() const = 0;

    /**
     * @brief Set configuration required by congestion control algorithm
     *
     * @param tcb internal congestion state
     */
    virtual void Init(Ptr<TcpSocketState> tcb [[maybe_unused]])
    {
    }

    /**
     * @brief Set rate operation required by the congestion control algorithm
     *
     * @param rateOps a pointer to the rate operation object used to manage rate control
     */
    virtual void SetRateOps(Ptr<TcpRateOps> rateOps [[maybe_unused]])
    {
    }

    /**
     * @brief Get the slow start threshold after a loss event
     *
     * Is guaranteed that the congestion control state (\p TcpAckState_t) is
     * changed BEFORE the invocation of this method.
     * The implementer should return the slow start threshold (and not change
     * it directly) because, in the future, the TCP implementation may require to
     * instantly recover from a loss event (e.g. when there is a network with an high
     * reordering factor).
     *
     * @param tcb internal congestion state
     * @param bytesInFlight total bytes in flight
     * @return Slow start threshold
     */
    virtual uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) = 0;

    /**
     * @brief Congestion avoidance algorithm implementation
     *
     * Mimic the function \pname{cong_avoid} in Linux. New segments have been ACKed,
     * and the congestion control duty is to update the window.
     *
     * The function is allowed to change directly cWnd and/or ssThresh.
     *
     * @param tcb internal congestion state
     * @param segmentsAcked count of segments acked
     */
    virtual void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);

    /**
     * @brief Timing information on received ACK
     *
     * The function is called every time an ACK is received (only one time
     * also for cumulative ACKs) and contains timing information. It is
     * optional (congestion controls need not implement it) and the default
     * implementation does nothing.
     *
     * @param tcb internal congestion state
     * @param segmentsAcked count of segments acked
     * @param rtt last rtt
     */
    virtual void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt);

    /**
     * @brief Trigger events/calculations specific to a congestion state
     *
     * This function mimics the notification function \pname{set_state} in Linux.
     * The function does not change the congestion state in the tcb; it notifies
     * the congestion control algorithm that this state is about to be changed.
     * The tcb->m_congState variable must be separately set; for example:
     *
     * @code
     *   m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_RECOVERY);
     *   m_tcb->m_congState = TcpSocketState::CA_RECOVERY;
     * @endcode
     *
     * @param tcb internal congestion state
     * @param newState new congestion state to which the TCP is going to switch
     */
    virtual void CongestionStateSet(Ptr<TcpSocketState> tcb,
                                    const TcpSocketState::TcpCongState_t newState);

    /**
     * @brief Trigger events/calculations on occurrence of congestion window event
     *
     * This function mimics the function \pname{cwnd_event} in Linux.
     * The function is called in case of congestion window events.
     *
     * @param tcb internal congestion state
     * @param event the event which triggered this function
     */
    virtual void CwndEvent(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCAEvent_t event);

    /**
     * @brief Returns true when Congestion Control Algorithm implements CongControl
     *
     * @return true if CC implements CongControl function
     *
     * This function is the equivalent in C++ of the C checks that are used
     * in the Linux kernel to see if an optional function has been defined.
     * Since CongControl is optional, not all congestion controls have it. But,
     * from the perspective of TcpSocketBase, the behavior is different if
     * CongControl is present. Therefore, this check should return true for any
     * congestion controls that implements the CongControl optional function.
     */
    virtual bool HasCongControl() const;

    /**
     * @brief Called when packets are delivered to update cwnd and pacing rate
     *
     * This function mimics the function cong_control in Linux. It is allowed to
     * change directly cWnd and pacing rate.
     *
     * @param tcb internal congestion state
     * @param rc Rate information for the connection
     * @param rs Rate sample (over a period of time) information
     */
    virtual void CongControl(Ptr<TcpSocketState> tcb,
                             const TcpRateOps::TcpRateConnection& rc,
                             const TcpRateOps::TcpRateSample& rs);

    // Present in Linux but not in ns-3 yet:
    /* call when ack arrives (optional) */
    //     void (*in_ack_event)(struct sock *sk, u32 flags);
    /* new value of cwnd after loss (optional) */
    //     u32  (*undo_cwnd)(struct sock *sk);
    /* hook for packet ack accounting (optional) */
    //     void (*pkts_acked)(struct sock *sk, u32 ext, int *attr, union tcp_cc_info *info);

    /**
     * @brief Copy the congestion control algorithm across sockets
     *
     * @return a pointer of the copied object
     */
    virtual Ptr<TcpCongestionOps> Fork() = 0;
};

/**
 * @brief The NewReno implementation
 *
 * New Reno introduces partial ACKs inside the well-established Reno algorithm.
 * This and other modifications are described in RFC 6582.
 *
 * @see IncreaseWindow
 */
class TcpNewReno : public TcpCongestionOps
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    TcpNewReno();

    /**
     * @brief Copy constructor.
     * @param sock object to copy.
     */
    TcpNewReno(const TcpNewReno& sock);

    ~TcpNewReno() override;

    std::string GetName() const override;

    void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;
    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;
    Ptr<TcpCongestionOps> Fork() override;

  protected:
    virtual uint32_t SlowStart(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);
    virtual void CongestionAvoidance(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);

  private:
    double m_betaLoss{0}; //!< Beta for New-Reno multiplicative decrease
    double m_betaEcn{0};  //!< Beta for New-Reno multiplicative decrease with ABE
};

} // namespace ns3

#endif // TCPCONGESTIONOPS_H
