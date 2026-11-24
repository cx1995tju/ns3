# HPCC 仿真操作手册

以下命令从 `ns-3-dev-git` 根目录执行，使用 Python 3 和本地 ns-3.49 构建环境。第一次运行建议先用自带配置完成两条流，再切换算法。模块结构与移植验证记录见[移植总结](README_HPCC.md)。

## 1. 构建和自检

```bash
./ns3 configure --enable-modules=hpcc --enable-tests --enable-examples \
  '--filter-module-examples-and-tests=hpcc;point-to-point'
./ns3 build -j4 scratch/hpcc/third test-runner
./test.py --no-build -s hpcc
./test.py --no-build -s devices-point-to-point
python3 contrib/hpcc/test/test_tools.py
```

两个 ns-3 套件应报告 `PASS`。Python 工具回归使用 Make 和 C++17 编译器，在临时目录构建并测试生成器和分析器，成功时报告 `OK`。要查看 `hpcc` 套件中每个用例的名称和结果，执行 `./ns3 run --no-build "test-runner --suite=hpcc --verbose"`。

## 2. 运行自带的 HPCC 实验

在同一个终端依次执行。把 `mix` 复制到临时目录，可以保留本次结果，并让配置中的相对路径按原样生效。

```bash
repo=$(pwd)
work=$(mktemp -d)
cp -R mix "$work/mix"
./ns3 run --no-build --cwd="$work" scratch/hpcc/third -- mix/config.txt
wc -l "$work/mix/fct.txt"
awk '{printf "%s -> %s  %.0f MB  %.3f ms\n", $1, $2, $5/1e6, $7/1e6}' "$work/mix/fct.txt"
```

默认拓扑有一台交换机，主机 2 和 3 在仿真时间 2 s 同时向主机 1 发送数据。预期 `fct.txt` 有两行，每行记录 200 MB。默认开启逐包 trace，本次运行的 `mix.tr` 约为 235 MiB。需要保留结果时，可用 `echo "$work"` 找到完整的实验目录。

## 3. 对比 HPCC、DCQCN 和 DCTCP

在上一步的实验目录中运行预设，三次实验会读取同一份拓扑和流量。脚本按算法生成配置和结果文件，便于比较。

```bash
(
  cd "$work" || exit 1
  for cc in hp dcqcn dctcp; do
    python3 "$repo/contrib/hpcc/run.py" --cc "$cc" --bw 100 --topo topology --trace flow || exit 1
  done
  wc -l mix/fct_topology_flow_*.txt
)
```

每种算法都应完成两条流。默认参数下，结果分别写入 `fct_topology_flow_hp95.txt`、`fct_topology_flow_dcqcn.txt` 和 `fct_topology_flow_dctcp.txt`。相同参数重跑会覆盖同名文件。脚本的 HPCC 预设与上游手写 `config.txt` 有各自的速率与遥测参数；比较算法时应记录生成的 `config_*.txt`。

| 参数 | 用法 |
| --- | --- |
| `--cc` | `hp`、`dcqcn`、`dctcp` 分别选择三种主算法；`timely` 和 `hpccPint` 也可用。 |
| `--topo`、`--trace` | 分别选择 `mix/<拓扑名>.txt` 和 `mix/<流量名>.txt`，传入文件名时省略扩展名。 |
| `--bw` | 主机网卡速率，单位 Gb/s，用于计算速率增量和 ECN 阈值等预设参数。仿真实际链路速率取自拓扑文件。 |
| `--rp-byte-reset` | DCQCN 字节阈值，默认 10000000（10 MB）。例如 `--cc dcqcn --rp-byte-reset 65536` 每发送 64 KiB 触发一次恢复，设为 0 可复现旧版纯定时器实验。 |
| `--enable_tr 1` | 为该次预设实验开启逐包记录。记录哪些节点由 `TRACE_FILE` 指定，预设使用 `mix/trace.txt`。 |

## 4. 自定义拓扑、流量与配置

三个输入文件均用空白符分隔字段。

- **拓扑文件**首行是节点总数、交换机数、链路数，其后列出交换机节点编号。每条链路占一行，依次给出两个端点、速率、传播时延和丢包率，例如 `0 1 100Gbps 0.001ms 0`。
- **流量文件**首行是要运行的流数，其后每行依次为源节点、目的节点、优先级、目的端口、数据量（字节）、开始时间（秒）。例如 `2 1 3 100 200000000 2` 表示主机 2 在 2 s 时发起一条 200 MB 流。源和目的应为主机，记录按开始时间非递减排列。仿真器只读取首行指定的记录数。
- **跟踪节点文件**首个数是节点数量，其后列出要跟踪的节点编号。

例如，要在原拓扑中运行两条各 1 MB 的流，可以在实验目录新增一个流量文件，再让脚本生成配置。

```bash
(
  cd "$work" || exit 1
  cat > mix/two_flows.txt <<'FLOWS'
2
2 1 3 100 1000000 2.0
3 1 3 100 1000000 2.0
FLOWS
  python3 "$repo/contrib/hpcc/run.py" --cc hp --bw 100 --topo topology --trace two_flows
)
```

手动调参时，复制一份生成的配置，再用第 2 节的 `./ns3 run` 命令传入新文件。常用参数如下。完整的上游参数解释见 [mix/config_doc.txt](mix/config_doc.txt)。

| 配置项 | 含义 |
| --- | --- |
| `CC_MODE` | 1 为 DCQCN，3 为 HPCC，8 为 DCTCP，7 为 TIMELY，10 为 HPCC-PINT。整组算法参数可从对应的脚本预设取得。 |
| `RP_BYTE_RESET` | 对应 `ns3::RdmaHw::RPByteReset`，仅用于 DCQCN。按网卡发出的 RDMA 数据帧大小计数，含协议头和重传，跨阈值的余数留给下一次。 |
| `SIMULATOR_STOP_TIME` | 仿真停止时间，单位秒。应覆盖最后一条流的开始时间及传输时间。 |
| `KMIN_MAP`、`KMAX_MAP`、`PMAX_MAP` | 格式为条目数，后跟若干对“链路速率（bit/s） 值”。交换机每种端口速率均要有对应条目。两个阈值以 1000 字节为单位，概率取 0 到 1。 |
| `QLEN_MON_START`、`QLEN_MON_END` | 交换机队列采样窗口，单位纳秒。 |
| `LINK_DOWN` | 三个值依次为相对 2 s 的偏移（微秒）和链路两端的节点编号；偏移大于零时触发。 |

DCQCN 每次因 CNP 执行降速时，会重启恢复定时器并将两个阶段计数和累计字节清零。此后，每过 `RP_TIMER` 微秒或每发送 `RP_BYTE_RESET` 字节，对应的计数加一，再更新速率。设 `F = FAST_RECOVERY_TIMES`，两项计数都小于 `F` 时执行快速恢复，两项都大于 `F` 时使用 `RATE_HAI`，其余情况使用 `RATE_AI`。字节阈值为零时沿用移植前的定时器阶段规则。[DCQCN 论文图 7](https://acn-2025.github.io/papers/DCQCN.pdf)

## 5. 按流大小分布生成流量

生成器使用 Python 3 标准库。沿用第 2 节的 `repo` 和 `work`，下面的示例在自带星形拓扑中选主机 1、2、3，按 AliStorage 分布生成流量。它单独保存配置、trace 和 FCT，便于随后分析。

```bash
gen="$repo/contrib/hpcc/traffic_gen"
python3 "$gen/traffic_gen.py" -c "$gen/AliStorage2019.txt" \
  -n 3 --first-host 1 -b 100G -l 0.1 -t 0.0002 --seed 7 -o "$work/mix/generated.txt"
head -n 4 "$work/mix/generated.txt"
sed -e 's|^FLOW_FILE .*|FLOW_FILE mix/generated.txt|' \
    -e 's|^FCT_OUTPUT_FILE .*|FCT_OUTPUT_FILE mix/fct_generated_hp.txt|' \
    -e 's|^TRACE_OUTPUT_FILE .*|TRACE_OUTPUT_FILE mix/generated.tr|' \
    -e 's|^PFC_OUTPUT_FILE .*|PFC_OUTPUT_FILE mix/pfc_generated.txt|' \
    -e 's|^QLEN_MON_FILE .*|QLEN_MON_FILE mix/qlen_generated.txt|' \
    -e 's|^SIMULATOR_STOP_TIME .*|SIMULATOR_STOP_TIME 2.02|' \
    -e 's|^QLEN_MON_END .*|QLEN_MON_END 2001000000|' \
    "$work/mix/config.txt" > "$work/mix/config_generated.txt"
./ns3 run --no-build --cwd="$work" scratch/hpcc/third -- mix/config_generated.txt
wc -l "$work/mix/fct_generated_hp.txt"
```

`-n` 个连续主机从 `--first-host` 开始编号。默认从 0 开始，适用于 `fat.txt` 的主机布局。每个源主机按泊松过程发起流，均匀选择另一个主机为目的地。`-l` 是相对主机带宽 `-b` 的目标负载比例，`-t` 是从仿真时间 2 s 起算的到达窗口，单位为秒。仿真停止时间应为最后一批流留出传输时间。固定 `--seed` 可重复生成相同的输入。以上命令在本次验证中生成了 24 条流，FCT 文件也有 24 行。

内置 CDF 有 `WebSearch_distribution.txt`（默认）、`FbHdp_distribution.txt`、`AliStorage2019.txt` 和 `GoogleRPC2008.txt`。自定义文件每行写“流大小（字节） 累计百分数”，两列严格递增，百分数从 0 到 100，相邻点间线性插值。

## 6. 读取结果和排查问题

| 默认文件 | 如何读取 |
| --- | --- |
| `mix/fct.txt` | 每条已完成流一行。八列依次为源 IPv4、目的 IPv4（均为十六进制）、源端口、目的端口、数据量（字节）、开始时间（ns）、实际 FCT（ns）、估算的独占链路 FCT（ns）。第 7 列除以第 8 列可得该流的 FCT 放大倍数。 |
| `mix/pfc.txt` | 每个接收的 PFC 事件一行，五列依次为时间（ns）、节点编号、节点类型（0 为主机，1 为交换机）、接口编号和动作（1 为暂停，0 为恢复）。仿真期间没有 PFC 事件时文件可以为空。 |
| `mix/qlen.txt` | 按时间输出各交换机端口的队列长度直方图。每行先给出节点和端口，再列出各长度区间的采样次数，每个区间宽 1000 字节。 |
| `mix/mix.tr` | 二进制文件，先记录端口速率和窗口设置，再记录逐包事件。可用本仓库的 `trace_reader` 读取。 |

### 查看逐包 trace

从仓库根目录用 Make 和 C++17 编译两个分析工具。下面的命令读取第 5 节生成的 trace，筛选交换机 0 在前 10 微秒记录的 UDP 数据包。

```bash
analysis="$repo/contrib/hpcc/analysis"
make -C "$analysis" OUT="$work/bin"
"$work/bin/trace_reader" "$work/mix/generated.tr" 'node=0&(time<2000010000&l3Prot=0x11)'
```

省略表达式会输出全部记录。过滤器接受十进制、十六进制、比较运算符、`&`、`|` 和括号。可以用 `queue=节点,接口,队列` 筛队列，用 `biflow=源IP,目的IP,源端口,目的端口` 同时筛数据及反方向 ACK。表达式用单引号传给 shell。输出依次给出时间（ns）、节点、接口和队列、队列字节数、事件以及协议字段。`U` 表示 UDP 数据，`A` 表示 ACK，`P` 表示 PFC。trace 使用本机二进制布局，跨机器读取时需要兼容的字节序与结构体布局。

### 按流大小分析 FCT

第 3 节的三个预设会生成同前缀的 FCT 文件。用 `-p` 指定包含目录的前缀，`-c` 按输出顺序列出文件名后缀。以下示例按大小将每组的两条流分成两桶。

```bash
"$work/bin/fct_analysis" -p "$work/mix/fct_topology_flow" -c hp95,dcqcn,dctcp -s 50
```

也可用生成流量时的 CDF 定义相同的字节边界，再分析第 5 节的输出。Python 3 版和 C++ 版支持相同的分桶参数。

```bash
python3 "$gen/flow_bins.py" -c "$gen/AliStorage2019.txt" > "$work/bins.txt"
python3 "$analysis/fct_analysis.py" -p "$work/mix/fct_generated" -c hp -S "$work/bins.txt"
```

每行的前两列是该桶的累计比例（0 到 1）和流大小上界（字节）。`-s` 按各算法各自的样本排名分桶，第二列取首个算法的上界；`-S` 按文件指定的共同大小上界分桶。其后每个算法依次占三列，分别是桶内 FCT 放大倍数的 P50、P95、P99，放大倍数按 `max(1, 实际 FCT / 独占链路 FCT)` 计算。空桶显示 `nan`。默认 `-t 0` 选目的端口 100 的普通流，`-t 1` 选端口 200 的 incast 流，`-t 2` 同时选两类。默认仅纳入完成时刻严格早于 3 s 的流，可用 `-T` 指定截止时间（ns）。

若提示打不开配置或拓扑文件，先检查 `--cwd` 指向的目录下是否存在 `mix/`。若 FCT 行数偏少，对照流量文件首行、最后一条流的开始时间和 `SIMULATOR_STOP_TIME`。更换拓扑后若报告缺少 ECN 阈值，按拓扑实际端口速率补全三个速率映射。
