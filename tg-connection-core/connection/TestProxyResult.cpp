#include <vector>

#include "StatsUtils.h" 
#include "TestProxyResult.h"
   
TestProxyResult::TestProxyResult(std::vector<double> ts) {
    for (double it : ts) ts_.push_back(it * 1000);
}

double TestProxyResult::p95() const {
    if (p95_ == -1) {
        p95_ = stats::percentile(ts_, 0.95);
    }

    return p95_;
}

double TestProxyResult::p50() const {
    if (p50_ == -1) {
        p50_ = stats::percentile(ts_, 0.50);
    }

    return p50_;
}

double TestProxyResult::jitter() const {
    if (jitter_ == -1) {
        jitter_ = stats::jitter(ts_);
    }

    return jitter_;
}

double TestProxyResult::success_rate() const {
    if (success_rate_ == -1) {
        int success_attempts = 0;
        for (auto it : ts_) {
            if (it > 0) success_attempts++;
        }

        success_rate_ = static_cast<double>(success_attempts) / ts_.size();
    }

    return success_rate_;
}