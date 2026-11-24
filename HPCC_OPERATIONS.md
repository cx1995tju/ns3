# HPCC 仿真操作手册

以下命令从 `ns-3-dev-git` 根目录执行，使用 Python 3 和本地 ns-3.49 构建环境。第一次运行建议先用自带配置完成两条流，再切换算法。模块结构与移植验证记录见[移植总结](README_HPCC.md)。

## 1. 构建和自检

```bash
./ns3 configure --enable-modules=hpcc --enable-tests --enable-examples \
  '--filter-module-examples-and-tests=hpcc;point-to-point'
./ns3 build -j4 scratch/hpcc/third test-runner
./test.py --no-build -s hpcc
./test.py --no-build -s devices-point-to-point
```

两项测试都应报告 `PASS`。要查看 `hpcc` 套件中每个用例的名称和结果，执行 `./ns3 run --no-build "test-runner --suite=hpcc --verbose"`。

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
| `SIMULATOR_STOP_TIME` | 仿真停止时间，单位秒。应覆盖最后一条流的开始时间及传输时间。 |
| `KMIN_MAP`、`KMAX_MAP`、`PMAX_MAP` | 格式为条目数，后跟若干对“链路速率（bit/s） 值”。交换机每种端口速率均要有对应条目。两个阈值以 1000 字节为单位，概率取 0 到 1。 |
| `QLEN_MON_START`、`QLEN_MON_END` | 交换机队列采样窗口，单位纳秒。 |
| `LINK_DOWN` | 三个值依次为相对 2 s 的偏移（微秒）和链路两端的节点编号；偏移大于零时触发。 |

## 5. 读取结果和排查问题

| 默认文件 | 如何读取 |
| --- | --- |
| `mix/fct.txt` | 每条已完成流一行。八列依次为源 IPv4、目的 IPv4（均为十六进制）、源端口、目的端口、数据量（字节）、开始时间（ns）、实际 FCT（ns）、估算的独占链路 FCT（ns）。第 7 列除以第 8 列可得该流的 FCT 放大倍数。 |
| `mix/pfc.txt` | 每个接收的 PFC 事件一行，五列依次为时间（ns）、节点编号、节点类型（0 为主机，1 为交换机）、接口编号和动作（1 为暂停，0 为恢复）。仿真期间没有 PFC 事件时文件可以为空。 |
| `mix/qlen.txt` | 按时间输出各交换机端口的队列长度直方图。每行先给出节点和端口，再列出各长度区间的采样次数，每个区间宽 1000 字节。 |
| `mix/mix.tr` | 二进制文件，先记录端口速率和窗口设置，再记录逐包事件。可用旧库的 `analysis/trace_reader` 读取。 |

如果旧库在本仓库旁边，可从仓库根目录编译读取器，用时间过滤默认实验中的首批发送事件。

```bash
upstream=../High-Precision-Congestion-Control
c++ -O2 -std=gnu++11 "$upstream/analysis/trace_reader.cpp" -o "$work/trace_reader"
"$work/trace_reader" "$work/mix/mix.tr" 'time=2000000000'
```

若提示打不开配置或拓扑文件，先检查 `--cwd` 指向的目录下是否存在 `mix/`。若 FCT 行数偏少，对照流量文件首行、最后一条流的开始时间和 `SIMULATOR_STOP_TIME`。更换拓扑后若报告缺少 ECN 阈值，按拓扑实际端口速率补全三个速率映射。
