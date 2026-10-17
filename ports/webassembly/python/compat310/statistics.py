"""Statistics APIs from Python 3.10 usable in a browser MicroPython VM.

The implementation favours bounded allocations and pure-Python algorithms.
Floating-point results are not guaranteed bit-for-bit identical to CPython.
"""

import math


class StatisticsError(ValueError):
    pass


def _data(values):
    return list(values)


def _sum(values):
    fsum = getattr(math, 'fsum', None)
    if fsum is not None:
        return fsum(values)
    return sum(values)


def mean(data):
    values = _data(data)
    if not values:
        raise StatisticsError('mean requires at least one data point')
    return _sum(values) / len(values)


def fmean(data, weights=None):
    values = _data(data)
    if not values:
        raise StatisticsError('fmean requires at least one data point')
    if weights is None:
        return float(_sum(values)) / len(values)
    weights = _data(weights)
    if len(values) != len(weights):
        raise StatisticsError('data and weights must be the same length')
    total = _sum(weights)
    if total == 0:
        raise StatisticsError('sum of weights must be non-zero')
    return _sum(x * w for x, w in zip(values, weights)) / total


def geometric_mean(data):
    values = _data(data)
    if not values:
        raise StatisticsError('geometric_mean requires at least one data point')
    if any(x <= 0 for x in values):
        raise StatisticsError('geometric mean requires a non-empty dataset containing positive numbers')
    return math.exp(_sum(math.log(x) for x in values) / len(values))


def harmonic_mean(data, weights=None):
    values = _data(data)
    if not values:
        raise StatisticsError('harmonic_mean requires at least one data point')
    if weights is None:
        weights = [1] * len(values)
    else:
        weights = _data(weights)
        if len(weights) != len(values):
            raise StatisticsError('data and weights must be the same length')
    if any(x < 0 for x in values) or any(w < 0 for w in weights):
        raise StatisticsError('harmonic mean does not support negative values')
    if any(x == 0 for x in values):
        return 0
    total_weight = _sum(weights)
    if total_weight <= 0:
        raise StatisticsError('sum of weights must be positive')
    return total_weight / _sum(w / x for x, w in zip(values, weights))


def median(data):
    values = sorted(data)
    n = len(values)
    if not n:
        raise StatisticsError('no median for empty data')
    mid = n // 2
    if n % 2:
        return values[mid]
    return (values[mid - 1] + values[mid]) / 2


def median_low(data):
    values = sorted(data)
    if not values:
        raise StatisticsError('no median for empty data')
    return values[(len(values) - 1) // 2]


def median_high(data):
    values = sorted(data)
    if not values:
        raise StatisticsError('no median for empty data')
    return values[len(values) // 2]


def median_grouped(data, interval=1):
    values = sorted(data)
    if not values:
        raise StatisticsError('no median for empty data')
    if interval <= 0:
        raise StatisticsError('interval must be positive')
    pivot = values[len(values) // 2]
    lower = pivot - interval / 2
    below = sum(v < pivot for v in values)
    within = sum(v == pivot for v in values)
    return lower + interval * (len(values) / 2 - below) / within


def multimode(data):
    counts = {}
    for value in data:
        counts[value] = counts.get(value, 0) + 1
    if not counts:
        return []
    largest = max(counts.values())
    return [value for value, count in counts.items() if count == largest]


def mode(data):
    choices = multimode(data)
    if not choices:
        raise StatisticsError('no mode for empty data')
    return choices[0]


def quantiles(data, *, n=4, method='exclusive'):
    if n < 1:
        raise StatisticsError('n must be at least 1')
    if method not in ('exclusive', 'inclusive'):
        raise ValueError('Unknown method: %r' % method)
    values = sorted(data)
    size = len(values)
    if size < 2:
        raise StatisticsError('must have at least two data points')
    cuts = []
    for i in range(1, n):
        position = ((size + 1) * i / n - 1) if method == 'exclusive' else ((size - 1) * i / n)
        # Exclusive interpolation is clamped to the lowest/highest sample.
        position = max(0, min(size - 1, position))
        lower = int(position)
        fraction = position - lower
        upper = min(lower + 1, size - 1)
        cuts.append(values[lower] + fraction * (values[upper] - values[lower]))
    return cuts


def _variance(data, mu, sample):
    values = list(data)
    size = len(values)
    minimum = 2 if sample else 1
    if size < minimum:
        raise StatisticsError('variance requires at least %d data points' % minimum)
    if mu is None:
        mu = mean(values)
    return _sum((v - mu) ** 2 for v in values) / (size - (1 if sample else 0))


def variance(data, xbar=None):
    return _variance(data, xbar, True)


def pvariance(data, mu=None):
    return _variance(data, mu, False)


def stdev(data, xbar=None):
    return math.sqrt(variance(data, xbar))


def pstdev(data, mu=None):
    return math.sqrt(pvariance(data, mu))


class NormalDist:
    """Normal distribution with Python 3.10's arithmetic and query methods."""

    def __init__(self, mu=0.0, sigma=1.0):
        if sigma < 0:
            raise StatisticsError('sigma must be non-negative')
        self.mu = float(mu)
        self.sigma = float(sigma)

    @classmethod
    def from_samples(cls, data):
        values = list(data)
        return cls(mean(values), stdev(values))

    def __repr__(self):
        return 'NormalDist(mu=%r, sigma=%r)' % (self.mu, self.sigma)

    def pdf(self, x):
        if self.sigma <= 0:
            raise StatisticsError('pdf() not defined when sigma is zero')
        z = (x - self.mu) / self.sigma
        return math.exp(-z * z / 2) / (self.sigma * math.sqrt(2 * math.pi))

    def cdf(self, x):
        if self.sigma <= 0:
            raise StatisticsError('cdf() not defined when sigma is zero')
        z = (x - self.mu) / (self.sigma * math.sqrt(2))
        erf = getattr(math, 'erf', None)
        if erf is not None:
            return (1 + erf(z)) / 2
        # Abramowitz-Stegun approximation for runtimes without math.erf.
        t = 1 / (1 + 0.3275911 * abs(z))
        poly = (((((1.061405429 * t - 1.453152027) * t)
                  + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t
        e = 1 - poly * math.exp(-z * z)
        return (1 + (e if z >= 0 else -e)) / 2

    def inv_cdf(self, p):
        if not 0 < p < 1:
            raise StatisticsError('p must be in the range 0.0 < p < 1.0')
        if self.sigma <= 0:
            raise StatisticsError('inv_cdf() not defined when sigma is zero')
        # Acklam's inverse Gaussian approximation, refined by the CDF.
        a = [-39.69683028665376, 220.9460984245205, -275.9285104469687,
             138.3577518672690, -30.66479806614716, 2.506628277459239]
        b = [-54.47609879822406, 161.5858368580409, -155.6989798598866,
             66.80131188771972, -13.28068155288572]
        c = [-0.007784894002430293, -0.3223964580411365, -2.400758277161838,
             -2.549732539343734, 4.374664141464968, 2.938163982698783]
        d = [0.007784695709041462, 0.3224671290700398,
             2.445134137142996, 3.754408661907416]
        if p < 0.02425:
            q = math.sqrt(-2 * math.log(p))
            z = (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / (
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q)+1)
        elif p > 1 - 0.02425:
            q = math.sqrt(-2 * math.log(1 - p))
            z = -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / (
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q)+1)
        else:
            q = p - 0.5
            r = q * q
            z = (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q / (
                (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r)+1)
        return self.mu + z * self.sigma

    def quantiles(self, n=4):
        return [self.inv_cdf(i / n) for i in range(1, n)]

    def zscore(self, x):
        if self.sigma <= 0:
            raise StatisticsError('zscore() not defined when sigma is zero')
        return (x - self.mu) / self.sigma

    def samples(self, n, *, seed=None):
        if n < 0:
            raise ValueError('number of samples must be non-negative')
        from random import Random
        generator = Random(seed)
        return [generator.gauss(self.mu, self.sigma) for _ in range(n)]

    def overlap(self, other):
        if not isinstance(other, NormalDist):
            raise TypeError('Expected another NormalDist instance')
        if self.sigma <= 0 or other.sigma <= 0:
            raise StatisticsError('overlap() not defined when sigma is zero')
        if self.mu == other.mu and self.sigma == other.sigma:
            return 1.0
        a = 1 / (self.sigma ** 2) - 1 / (other.sigma ** 2)
        b = -2 * self.mu / (self.sigma ** 2) + 2 * other.mu / (other.sigma ** 2)
        c = ((self.mu / self.sigma) ** 2 -
             (other.mu / other.sigma) ** 2 -
             2 * math.log(other.sigma / self.sigma))
        if abs(a) < 1e-14:
            crossings = [-c / b]
        else:
            discriminant = b * b - 4 * a * c
            crossings = [] if discriminant <= 0 else sorted((
                (-b - math.sqrt(discriminant)) / (2 * a),
                (-b + math.sqrt(discriminant)) / (2 * a),
            ))
        boundaries = [float('-inf')] + crossings + [float('inf')]
        total = 0.0
        for left, right in zip(boundaries, boundaries[1:]):
            if left == float('-inf'):
                center = right - 2 * max(self.sigma, other.sigma)
            elif right == float('inf'):
                center = left + 2 * max(self.sigma, other.sigma)
            else:
                center = (left + right) / 2
            selected = self if self.pdf(center) < other.pdf(center) else other
            total += selected.cdf(right) - selected.cdf(left)
        return min(1.0, max(0.0, total))

    def __add__(self, other):
        if isinstance(other, NormalDist):
            return NormalDist(self.mu + other.mu, math.hypot(self.sigma, other.sigma))
        return NormalDist(self.mu + other, self.sigma)

    def __sub__(self, other):
        if isinstance(other, NormalDist):
            return NormalDist(self.mu - other.mu, math.hypot(self.sigma, other.sigma))
        return NormalDist(self.mu - other, self.sigma)

    def __mul__(self, other):
        return NormalDist(self.mu * other, abs(self.sigma * other))

    __rmul__ = __mul__
    __radd__ = __add__

    def __truediv__(self, other):
        return NormalDist(self.mu / other, self.sigma / abs(other))

    def __neg__(self):
        return NormalDist(-self.mu, self.sigma)

    def __pos__(self):
        return self


__all__ = [
    'StatisticsError', 'mean', 'fmean', 'geometric_mean', 'harmonic_mean',
    'median', 'median_low', 'median_high', 'median_grouped', 'mode', 'multimode',
    'quantiles', 'variance', 'pvariance', 'stdev', 'pstdev', 'NormalDist',
]
