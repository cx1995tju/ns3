"""Print HPCC flow-size bin limits and their cumulative percentiles."""

import argparse

from custom_rand import DEFAULT_CDF, read_cdf


def flow_bins(cdf):
    percentile = 100
    size = int(cdf.getValueFromPercentile(percentile))
    bins = []
    while size > 0:
        if not bins or size != bins[-1]:
            bins.append(size)
        if percentile <= 1:
            break
        percentile = max(1, percentile - 1)
        next_size = int(cdf.getValueFromPercentile(percentile))
        if next_size < size // 2 and size > 1000:
            next_size = size // 2
            percentile = cdf.getPercentileFromValue(next_size)
        size = next_size
    return reversed(bins)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-c", "--cdf", default=DEFAULT_CDF)
    args = parser.parse_args()
    try:
        distribution = read_cdf(args.cdf)
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")
    for limit in flow_bins(distribution):
        print(limit, distribution.getPercentileFromValue(limit))
