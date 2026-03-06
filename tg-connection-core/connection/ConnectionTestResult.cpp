#include "ConnectionTestResult.h"

#include <iomanip>
#include <sstream>

#include <td/telegram/td_api.h>

namespace td_api = td::td_api;

// ===== SUCCESS =====

ConnectionTestResult::ConnectionTestResult(
    double final_score_,
    double tp_score_,
    double tcp_score_,
    double media_score_,
    std::vector<Record> tcp_records_,
    TestProxyResult test_proxy_res_,
    std::unordered_map<std::int32_t, std::uint64_t> state_counters_
)
    : is_error(false),
      final_score(final_score_),
      tp_score(tp_score_),
      tcp_score(tcp_score_),
      media_score(media_score_),
      tcp_records(std::move(tcp_records_)),
      test_proxy_res(std::move(test_proxy_res_)),
      state_counters(std::move(state_counters_)) {}

// ===== ERROR =====

ConnectionTestResult::ConnectionTestResult(
    bool is_error_,
    std::string message
)
    : is_error(is_error_),
      error_message(std::move(message)),
      final_score(0),
      tp_score(0),
      tcp_score(0),
      media_score(0),
      tcp_records({}),
      test_proxy_res({}),
      state_counters({}) {}

std::string ConnectionTestResult::summary() const {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);

    if (is_error) {
        out << "Telegram connection test failed\n";
        out << "Reason: " << error_message << "\n";
        return out.str();
    }

    out << "Telegram connection quality: "
        << quality_label(final_score)
        << " (" << final_score << "/100)\n";

    out << "  MTProto score: " << tp_score << "/100\n";
    out << "  TCP score:     " << tcp_score << "/100\n";
    out << "  Media score:     " << tcp_score << "/100\n";

    out << "  TestProxy:\n";
    out << "    p50=" << test_proxy_res.p50() << "ms"
        << ", p95=" << test_proxy_res.p95() << "ms"
        << ", jitter=" << test_proxy_res.jitter() << "ms"
        << ", success=" << (test_proxy_res.success_rate() * 100) << "%\n";

    out << "  TCP (best DCs):\n";
    for (const auto& rec : tcp_records) {
        out << "    "
            << rec.ip_address().get_ip_str().str()
            << ":" << rec.ip_address().get_port()
            << " p50=" << rec.p50() << "ms"
            << " p95=" << rec.p95() << "ms"
            << " success=" << (rec.success_rate() * 100) << "%\n";
    }

    out << "  Connection states:\n";
    for (const auto& [id, count] : state_counters) {
        out << "    "
            << connection_state_name(id)
            << ": " << count << "\n";
    }

    return out.str();
}

ConnectionTestResult ConnectionTestResult::error(std::string message) {
    return ConnectionTestResult(true, std::move(message));
}

const char* ConnectionTestResult::quality_label(double score) {
    if (score >= 90) return "Excellent";
    if (score >= 75) return "Good";
    if (score >= 55) return "Fair";
    if (score >= 30) return "Poor";
    return "Bad";
}

const char* ConnectionTestResult::connection_state_name(std::int32_t id) {
    switch (id) {
        case td_api::connectionStateWaitingForNetwork::ID:
            return "WaitingForNetwork";
        case td_api::connectionStateConnectingToProxy::ID:
            return "ConnectingToProxy";
        case td_api::connectionStateConnecting::ID:
            return "Connecting";
        case td_api::connectionStateUpdating::ID:
            return "Updating";
        case td_api::connectionStateReady::ID:
            return "Ready";
        default:
            return "Unknown";
    }
}
