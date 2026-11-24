# HPCC 数据中心仿真移植总结

本仓库把上游 `High-Precision-Congestion-Control` 提交 `9f4be2a` 中基于 ns-3.18 的 RDMA 仿真移植到了 ns-3.49。主要交付是可独立启用的 `hpcc` 模块、从上游 `simulation/scratch/third.cc` 移植的实验入口，以及可直接运行的拓扑和流量输入。构建命令、实验复现和输出字段见[中文操作手册](HPCC_OPERATIONS.md)。

## 算法与数据路径

发送端由 `RdmaClient` 发起流，`RdmaDriver` 将队列对分配给网卡，`RdmaHw` 根据 ACK/NACK、在途窗口和当前速率调度发送。交换机按 ECMP 表选出端口，在出口按队列积压标记 ECN，在入口按优先级触发 PFC。接收端在 RDMA ACK 中回送拥塞标记和遥测信息。

| 算法 | `CC_MODE` | 仿真行为 |
| --- | --- | --- |
| HPCC | 3 | 交换机记录每跳时间、已发送字节、队列长度和链路速率，发送端从 ACK 中取回 INT 并更新速率。 |
| DCQCN | 1 | 接收端回显 ECN，发送端更新拥塞估计并降速，此后由每个 QP 的定时器和已发送字节数独立触发速率恢复。 |
| DCTCP | 8 | 使用上游的硬件 RDMA 模型，按线速启动，在发送端依据收到的标记比例调整速率。 |

同一入口还支持 TIMELY（模式 7）和 HPCC-PINT（模式 10）。HPCC-PINT 按配置使用一或两个字节的遥测字段。

## 改动位置

| 位置 | 内容 |
| --- | --- |
| [链路与设备模型](contrib/hpcc/model/qbb-net-device.cc) | `QbbNetDevice`、`QbbChannel`、RDMA 出口调度、按优先级暂停与恢复；`QbbHelper` 负责安装和跟踪。 |
| [RDMA 主机模型](contrib/hpcc/model/rdma-hw.cc) | `rdma-driver`、`rdma-hw`、`rdma-queue-pair`、`rdma-client` 及其 helper。适配了 ns-3.49 的发送计时、事件、回调和属性系统。 |
| [交换机与队列](contrib/hpcc/model/switch-node.cc) | `switch-node`、`switch-mmu`、`broadcom-egress-queue` 实现转发、共享缓冲、ECN 和 PFC。单独移植了 `leaky-bucket` 有界 FIFO 及其 helper。 |
| [报文与遥测](contrib/hpcc/model/custom-header.cc) | `qbb-header`、`pause-header`、`cn-header`、`int-header`、`pint` 和 `custom-header`。专用链路层头按上游模型计入 14 字节，交换机通过 ns-3 的包头接口读写遥测。 |
| [包事件格式](contrib/hpcc/model/trace-format.h) 和 [仿真设置](contrib/hpcc/helper/sim-setting.h) | 保留上游二进制 trace 布局。旧库的 `analysis/trace_reader` 已成功解析本次生成的文件。 |
| [构建](contrib/hpcc/CMakeLists.txt) 和 [实验入口](scratch/hpcc/third.cc) | 接入 CMake 和 `./ns3`，移植拓扑加载、流量调度、FCT/PFC 输出及队列采样。输入位于 [mix](mix/)，三种算法的参数预设由 [Python 3 脚本](contrib/hpcc/run.py) 生成。 |

[流量生成工具](contrib/hpcc/traffic_gen/)提供四份上游 CDF，可生成实验流量及流大小分桶；[分析工具](contrib/hpcc/analysis/)支持逐包 trace 过滤与多算法 FCT 分位数比较。

ns-3 原有模块的改动集中在 `PointToPointNetDevice`。接收处理和一次发送完成后的下一包选择支持虚函数分派，QBB 可以复用点到点设备的发送状态和信道计时，同时执行按优先级和队列对的调度。[基类改动位置](src/point-to-point/model/point-to-point-net-device.h)

## 验证结果

- [hpcc 测试套件](contrib/hpcc/test/hpcc-test-suite.cc)的七个用例均通过。两个用例检查队列调度与记账、PINT 字段宽度与 INT 计数回绕。两个 DCQCN 用例检查字节触发、定时触发、阶段切换、降速复位及纯定时器兼容模式。另三个用例让两个 100 Gb/s 发送端争用一个 25 Gb/s 出口，分别验证 HPCC、DCQCN 和 DCTCP 的最终 ACK、ECN 回显、发送端降速和 PFC 恢复；HPCC 还检查 ACK 中的瓶颈速率和积压量。
- 已用 AliStorage 分布生成并完成 24 条流。新旧读取器对这次仿真的 5,104 条 trace 记录输出一致，Python 和 C++ 两个分析器给出的 FCT 分桶统计一致。
- 原有的 `devices-point-to-point` 回归通过。原版 `mix/config.txt` 选择 HPCC，本次运行中，两条 200 MB 流均完成，流完成时间约为 **36.14 ms** 和 **36.69 ms**。这两个数值供复现实验时参考。
- 已运行 `fat.txt` 和 `ali_32host_10rack.txt` 的跨机架流量，以及多跳转发、链路中断后重路由、TIMELY 和两种宽度的 HPCC-PINT。验证范围为 ns-3.49 的功能和 trace 可读性，与 ns-3.18 的逐包时序对照尚未进行。
