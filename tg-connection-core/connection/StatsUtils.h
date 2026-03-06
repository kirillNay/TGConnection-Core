#include <vector>

namespace stats {
double jitter(const std::vector<double>& data);

double percentile(const std::vector<double>& data, double percentile);

double median(const std::vector<double>& data);

double clamp(double x);

double clamp(double x, double a, double b);

double lerp(double x, double a, double b);

double tail_rate(double p50, double p95);

double jitter_rate(double jitter, double p50);

}