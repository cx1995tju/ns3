"""Linear interpolation of the HPCC flow-size CDF (bytes, cumulative percent)."""

import bisect
import math
import random
from pathlib import Path

DEFAULT_CDF = Path(__file__).with_name("WebSearch_distribution.txt")


class CustomRand:
    def testCdf(self, cdf):
        return (len(cdf) >= 2 and cdf[0][0] >= 0 and cdf[0][1] == 0 and cdf[-1][1] == 100
                and all(math.isfinite(x) and math.isfinite(y) for x, y in cdf)
                and all(x < u and y < v for (x, y), (u, v) in zip(cdf, cdf[1:])))

    def setCdf(self, cdf):
        if not self.testCdf(cdf):
            return False
        self.cdf = list(cdf)
        self.sizes, self.percentiles = zip(*cdf)
        return True

    @staticmethod
    def _interpolate(value, xs, ys):
        i = bisect.bisect_left(xs, value)
        if i == 0:
            return ys[0]
        return ys[i - 1] + (ys[i] - ys[i - 1]) * (value - xs[i - 1]) / (xs[i] - xs[i - 1])

    def getPercentileFromValue(self, x):
        if x < 0 or x > self.sizes[-1]:
            return -1
        return self._interpolate(x, self.sizes, self.percentiles)

    def getValueFromPercentile(self, y):
        if not 0 <= y <= 100:
            raise ValueError("percentile must be between 0 and 100")
        return self._interpolate(y, self.percentiles, self.sizes)

    def rand(self):
        return self.getValueFromPercentile(random.random() * 100)

    def getIntegralY(self, y):
        end_size = self.getValueFromPercentile(y)
        total = 0.0
        for (x0, y0), (x1, y1) in zip(self.cdf, self.cdf[1:]):
            if y <= y1:
                total += (x0 + end_size) * (y - y0) / 2
                break
            total += (x0 + x1) * (y1 - y0) / 2
        return total / 100

    def getAvg(self):
        return self.getIntegralY(100)


def read_cdf(path):
    points = []
    with open(path, encoding="utf-8") as source:
        for number, line in enumerate(source, 1):
            if line.strip():
                try:
                    x, y = map(float, line.split())
                except ValueError as exc:
                    raise ValueError(f"{path}:{number}: expected size and percentile") from exc
                points.append((x, y))
    distribution = CustomRand()
    if not distribution.setCdf(points):
        raise ValueError(f"{path}: expected strictly increasing sizes and percentages from 0 to 100")
    return distribution
