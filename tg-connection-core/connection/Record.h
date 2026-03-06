#pragma once

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <vector>

struct Record {

public:
    Record(std::vector<double> rtt_mss, td::IPAddress ip_address, int attempts, int success);

    double success_rate() const;

    double p50() const;

    double p95() const;

    double jitter() const;

    double min() const;

    double max() const;

    double attempts() const;

    double success() const;

    const td::IPAddress& ip_address() const;

private:
    std::vector<double> rtt_mss_;
    td::IPAddress ip_address_;
    int attempts_;
    int success_;

    mutable double p50_{-1};
    mutable double p95_{-1};
    mutable double jitter_{-1};
  
};