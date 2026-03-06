#pragma once

#include <vector>

#include "StatsUtils.h"

struct TestProxyResult {

public:
    TestProxyResult(std::vector<double> ts);

    double p95() const;
    double p50() const;
    double jitter() const;
    double success_rate() const;

private:
    std::vector<double> ts_;

    mutable double p95_{-1};
    mutable double p50_{-1};
    mutable double jitter_{-1};
    mutable double success_rate_{-1};

};
