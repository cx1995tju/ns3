"""Group completed flows by size and print the median, p95 and p99 FCT slowdown."""

import argparse
import bisect
import math


def read_flows(path, kind, cutoff):
    flows = []
    ports = ((100,), (200,), (100, 200))[kind]
    with open(path, encoding="ascii") as source:
        for number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                _, _, _, port, size, start, fct, standalone = line.split()
                port, size, start, fct, standalone = map(int, (port, size, start, fct, standalone))
                if standalone <= 0 or min(size, start, fct) < 0:
                    raise ValueError
            except ValueError as error:
                raise ValueError(f"{path}:{number}: invalid FCT record") from error
            if port in ports and start + fct < cutoff:
                flows.append((size, max(1.0, fct / standalone)))
    flows.sort(key=lambda flow: flow[0])
    return flows


def read_steps(path):
    steps = []
    with open(path, encoding="ascii") as source:
        for line in source:
            if line.strip():
                size, percent = line.split()
                size, percent = int(size), float(percent)
                if (size < 0 or not 0 <= percent <= 100
                        or (steps and (size <= steps[-1][0] or percent < steps[-1][1]))):
                    raise ValueError(f"{path}: invalid size/percentile boundary")
                steps.append((size, percent))
    if not steps:
        raise ValueError(f"{path}: no bin boundaries")
    return steps


def summarize(args):
    steps = read_steps(args.steps) if args.steps else [
        (None, min(p + args.step, 100)) for p in range(0, 100, args.step)
    ]
    rows = [[percent / 100] for _, percent in steps]
    for index, cc in enumerate(args.cc.split(",")):
        flows = read_flows(f"{args.prefix}_{cc}.txt", args.type, args.time_limit)
        sizes = [size for size, _ in flows]
        left = 0
        for row, (limit, percent) in zip(rows, steps):
            right = (bisect.bisect_right(sizes, limit, lo=left) if limit is not None
                     else len(flows) * percent // 100)
            values = sorted(slowdown for _, slowdown in flows[left:right])
            if index == 0:
                row.append(limit if limit is not None else sizes[right - 1] if values else 0)
            for percentile in (50, 95, 99):
                row.append(values[len(values) * percentile // 100] if values else math.nan)
            left = right
    for percent, size, *slowdowns in rows:
        print(f"{percent:.6f} {size}", *(f"{v:.3f}" for v in slowdowns), sep="\t")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-p", dest="prefix", default="mix/fct_fat", help="input prefix including directory")
    parser.add_argument("-c", dest="cc", required=True, help="comma-separated suffixes in output order")
    parser.add_argument("-s", dest="step", type=int, default=5, help="size percentile step")
    parser.add_argument("-S", dest="steps", help="file of size and cumulative percentile limits")
    parser.add_argument("-t", dest="type", type=int, choices=(0, 1, 2), default=0,
                        help="0: normal, 1: incast, 2: both")
    parser.add_argument("-T", dest="time_limit", type=int, default=3_000_000_000,
                        help="exclusive flow completion cutoff in ns")
    parser.add_argument("-b", type=int, help=argparse.SUPPRESS)  # Accepted by the upstream script.
    args = parser.parse_args()
    if not 1 <= args.step <= 100 or args.time_limit < 0 or not all(args.cc.split(",")):
        parser.error("invalid step, cutoff or CC list")
    try:
        summarize(args)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
