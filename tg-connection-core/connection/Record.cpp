#include "Record.h"
#include "StatsUtils.h"

#include <algorithm>
#include <cmath>

Record::Record(std::vector<double> rtt_mss, td::IPAddress ip_address, int attempts, int success) : 
    rtt_mss_(std::move(rtt_mss)),
    ip_address_(std::move(ip_address)),
    attempts_(attempts),
    success_(success){};

double Record::success_rate() const {
    return static_cast<double>(success_) / attempts_;
}

double Record::p50() const {
    if (p50_ == -1) {
        p50_ = stats::percentile(rtt_mss_, 0.50);
    }

    return p50_;
}

double Record::p95() const {
    if (p95_ == -1) {
        p95_ = stats::percentile(rtt_mss_, 0.95);
    }

    return p95_;
}

double Record::jitter() const {
    if (jitter_ == -1) {
        jitter_ = stats::jitter(rtt_mss_);
    }

    return jitter_;
}

double Record::min() const {
    return *std::min_element(rtt_mss_.begin(), rtt_mss_.end());
}

double Record::max() const {
    return *std::max_element(rtt_mss_.begin(), rtt_mss_.end());
}

double Record::attempts() const {
    return attempts_;
}

double Record::success() const {
    return success_;
}

const td::IPAddress& Record::ip_address() const {
    return ip_address_;
}
