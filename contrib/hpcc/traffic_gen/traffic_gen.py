"""Generate time-ordered HPCC flows with independent Poisson arrivals at each host."""

import argparse
import heapq
import math
import random

from custom_rand import DEFAULT_CDF, read_cdf


def bandwidth(value):
    scales = {"K": 1e3, "M": 1e6, "G": 1e9}
    unit = value[-1:].upper()
    return float(value[:-1]) * scales[unit] if unit in scales else float(value)


def generate(args, cdf, out):
    mean_gap = cdf.getAvg() * 8e9 / (args.bandwidth * args.load)
    if not math.isfinite(mean_gap) or mean_gap <= 0:
        raise ValueError("invalid mean inter-arrival time")
    rng = random.Random(args.seed)
    # Keep sub-nanosecond gaps until writing the simulator's nanosecond timestamps.
    arrivals = [(rng.expovariate(1 / mean_gap), host) for host in range(args.nhost)]
    heapq.heapify(arrivals)
    end = args.time * 1e9
    count = 0
    # Fixed header width lets us stream large workloads and backfill the exact count.
    out.write(f"{0:<20}\n")
    while arrivals[0][0] <= end:
        when, src = arrivals[0]
        dst = rng.randrange(args.nhost - 1)
        if dst >= src:
            dst += 1
        size = max(1, int(cdf.getValueFromPercentile(rng.random() * 100)))
        ns = 2_000_000_000 + int(when)
        out.write(f"{src + args.first_host} {dst + args.first_host} 3 100 {size} "
                  f"{ns // 10**9}.{ns % 10**9:09d}\n")
        count += 1
        heapq.heapreplace(arrivals, (when + rng.expovariate(1 / mean_gap), src))
    out.seek(0)
    out.write(f"{count:<20}\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-c", "--cdf", default=DEFAULT_CDF)
    parser.add_argument("-n", "--nhost", type=int, required=True, help="number of consecutive hosts")
    parser.add_argument("--first-host", type=int, default=0, help="first host node ID")
    parser.add_argument("-l", "--load", type=float, default=0.3, help="per-host offered load fraction")
    parser.add_argument("-b", "--bandwidth", type=bandwidth, default="10G", help="per-host bit/s, suffix K/M/G")
    parser.add_argument("-t", "--time", type=float, default=10, help="arrival window in seconds starting at 2 s")
    parser.add_argument("-o", "--output", default="tmp_traffic.txt")
    parser.add_argument("--seed", type=int, help="reproduce a workload")
    args = parser.parse_args()
    if args.nhost < 2 or args.first_host < 0:
        parser.error("specify at least two hosts and a nonnegative first host ID")
    if not all(math.isfinite(x) and x > 0 for x in (args.load, args.bandwidth, args.time)):
        parser.error("load, bandwidth and time must be positive finite numbers")
    try:
        cdf = read_cdf(args.cdf)
        with open(args.output, "w", encoding="ascii") as output:
            generate(args, cdf, output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
