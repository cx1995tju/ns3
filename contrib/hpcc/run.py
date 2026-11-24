import argparse
import subprocess
from pathlib import Path

config_template = """ENABLE_QCN 1
USE_DYNAMIC_PFC_THRESHOLD 1

PACKET_PAYLOAD_SIZE 1000

TOPOLOGY_FILE mix/{topo}.txt
FLOW_FILE mix/{trace}.txt
TRACE_FILE mix/trace.txt
TRACE_OUTPUT_FILE mix/mix_{topo}_{trace}_{cc}{failure}.tr
FCT_OUTPUT_FILE mix/fct_{topo}_{trace}_{cc}{failure}.txt
PFC_OUTPUT_FILE mix/pfc_{topo}_{trace}_{cc}{failure}.txt

SIMULATOR_STOP_TIME 4.00

CC_MODE {mode}
ALPHA_RESUME_INTERVAL {t_alpha}
RATE_DECREASE_INTERVAL {t_dec}
CLAMP_TARGET_RATE 0
RP_TIMER {t_inc}
EWMA_GAIN {g}
FAST_RECOVERY_TIMES 1
RATE_AI {ai}Mb/s
RATE_HAI {hai}Mb/s
MIN_RATE 1000Mb/s
DCTCP_RATE_AI {dctcp_ai}Mb/s

ERROR_RATE_PER_LINK 0.0000
L2_CHUNK_SIZE 4000
L2_ACK_INTERVAL 1
L2_BACK_TO_ZERO 0

HAS_WIN {has_win}
GLOBAL_T 1
VAR_WIN {vwin}
FAST_REACT {us}
U_TARGET {u_tgt}
MI_THRESH {mi}
INT_MULTI {int_multi}
MULTI_RATE 0
SAMPLE_FEEDBACK 0
PINT_LOG_BASE {pint_log_base}
PINT_PROB {pint_prob}

RATE_BOUND 1

ACK_HIGH_PRIO {ack_prio}

LINK_DOWN {link_down}

ENABLE_TRACE {enable_tr}

KMAX_MAP {kmax_map}
KMIN_MAP {kmin_map}
PMAX_MAP {pmax_map}
BUFFER_SIZE {buffer_size}
QLEN_MON_FILE mix/qlen_{topo}_{trace}_{cc}{failure}.txt
QLEN_MON_START 2000000000
QLEN_MON_END 3000000000
"""
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate the HPCC configuration and run ns-3")
    parser.add_argument(
        "--cc", default="hp",
        choices=("hp", "dcqcn", "dcqcn_paper", "dcqcn_vwin", "dcqcn_paper_vwin",
                 "dctcp", "timely", "timely_vwin", "hpccPint"),
    )
    parser.add_argument("--trace", default="flow", help="flow file stem in mix/")
    parser.add_argument("--bw", type=int, default=100, help="NIC bandwidth in Gb/s")
    parser.add_argument("--down", default="0 0 0", help="link-down time and endpoints")
    parser.add_argument("--topo", default="fat", help="topology file stem in mix/")
    parser.add_argument("--utgt", type=int, default=95, help="HPCC target utilization in percent")
    parser.add_argument("--mi", type=int, default=0, help="MI_THRESH")
    parser.add_argument("--hpai", type=int, default=0, help="HPCC additive increase in Mb/s")
    parser.add_argument("--pint_log_base", type=float, default=1.01)
    parser.add_argument("--pint_prob", type=float, default=1.0)
    parser.add_argument("--enable_tr", type=int, choices=(0, 1), default=0,
                        help="dump packet-level events")
    args = parser.parse_args()

    bw = args.bw
    if bw <= 0:
        parser.error("--bw must be positive")
    cc = args.cc
    failure = "_down" if args.down != "0 0 0" else ""
    rates = [bw, 4 * bw]

    def ecn_map(values):
        return "2 " + " ".join(f"{r * 10**9} {value}" for r, value in zip(rates, values))

    params = dict(topo=args.topo, trace=args.trace, cc=cc, failure=failure,
                  t_alpha=1, t_dec=4, t_inc=300, g=0.00390625,
                  dctcp_ai=1000, has_win=0, vwin=0, us=0, int_multi=1, ack_prio=1,
                  u_tgt=args.utgt / 100, mi=args.mi, pint_log_base=args.pint_log_base,
                  pint_prob=args.pint_prob, link_down=args.down, enable_tr=args.enable_tr,
                  kmax_map=ecn_map([400 * r // 25 for r in rates]),
                  kmin_map=ecn_map([100 * r // 25 for r in rates]),
                  pmax_map=ecn_map([0.2, 0.2]), buffer_size=16 * bw // 50)

    if cc in ("hp", "hpccPint"):
        ai = args.hpai or 10 * bw // 25
        label = f"{cc}{args.utgt}"
        if args.mi > 0:
            label += f"mi{args.mi}"
        if args.hpai > 0:
            label += f"ai{ai}"
        if cc == "hpccPint":
            label += f"log{args.pint_log_base:.3f}p{args.pint_prob:.3f}"
        params.update(mode=3 if cc == "hp" else 10, cc=label, ai=ai, hai=ai,
                      int_multi=max(1, bw // 25), has_win=1, vwin=1, us=1, ack_prio=0)
    elif cc.startswith("dcqcn"):
        params.update(mode=1, ai=5 * bw // 25, hai=50 * bw // 25)
        if "paper" in cc:
            params.update(t_alpha=50, t_dec=50, t_inc=55)
    elif cc == "dctcp":
        threshold = ecn_map([30 * r // 10 for r in rates])
        params.update(mode=8, ai=10, hai=10, g=0.0625, dctcp_ai=615,
                      has_win=1, vwin=1, ack_prio=0,
                      kmax_map=threshold, kmin_map=threshold, pmax_map=ecn_map([1.0, 1.0]))
    else:
        params.update(mode=7, ai=bw, hai=5 * bw)
    if cc.endswith("_vwin"):
        params.update(has_win=1, vwin=1, ack_prio=0)

    config_name = "mix/config_{topo}_{trace}_{cc}{failure}.txt".format(**params)
    with open(config_name, "w", encoding="utf-8") as config:
        config.write(config_template.format(**params))
    ns3_root = Path(__file__).resolve().parents[2]
    subprocess.run(
        [ns3_root / "ns3", "run", "--cwd", Path.cwd(), "scratch/hpcc/third", "--", config_name],
        cwd=ns3_root,
        check=True,
    )
