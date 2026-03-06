#include <vector>

#include "StatsUtils.h"

namespace stats {

double jitter(const std::vector<double>& data) {
    double sum = 0;
    for (double r : data) sum += r;
    double avg = sum / data.size();

    double sum_jitter = 0.0;
    for (double rtt : data) {
        sum_jitter += std::abs(rtt - avg);
    }
    return sum_jitter / data.size();
}

double median(const std::vector<double>& data) {
    return percentile(data, 0.5);
}

double percentile(const std::vector<double>& data, double percentile) {
    if (data.empty()) {
        return 0.0;
    }

    if (percentile <= 0.0) {
        return *std::min_element(data.begin(), data.end());
    }

    if (percentile >= 1.0) {
        return *std::max_element(data.begin(), data.end());
    }

    std::vector<double> sorted = data;
    std::sort(sorted.begin(), sorted.end());

    const double index = percentile * (data.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(index));
    const size_t upper = static_cast<size_t>(std::ceil(index));

    if (lower == upper) {
        return sorted[lower];
    }

    const double weight = index - lower;
    return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
}

double clamp(double x) {
    return clamp(x, 0.0, 1.0);
}

double clamp(double x, double a, double b) {
    return std::min(b, std::max(a, x));
}

double lerp(double x, double a, double b) {
    return clamp((x - a) / (b - a));
}

double tail_rate(double p50, double p95) {
    if (p50 <= 0) return 1.0;
    double ratio = p95 / p50;
    return lerp(ratio, 1.1, 2.0);
}

double jitter_rate(double jitter, double p50) {
    if (p50 <= 0) return 1.0;
    double ratio = jitter / p50;
    return lerp(ratio, 0.1, 1.0);
}

}
