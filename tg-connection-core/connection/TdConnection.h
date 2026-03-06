#pragma once

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include "Record.h"
#include "td/utils/Promise.h"
#include "ConnectionTestResult.h"

#include <thread>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <unordered_map>
#include <cstdint>
#include "ExternalLogger.h"

namespace detail {
template <class... Fs>
struct overload;

template <class F>
struct overload<F> : public F {
  explicit overload(F f) : F(f) {
  }
};
template <class F, class... Fs>
struct overload<F, Fs...>
    : public overload<F>
    , public overload<Fs...> {
  overload(F f, Fs... fs) : overload<F>(f), overload<Fs...>(fs...) {
  }
  using overload<F>::operator();
  using overload<Fs...>::operator();
};
}  // namespace detail

template <class... F>
auto overloaded(F... f) {
  return detail::overload<F...>(f...);
}

namespace td_api = td::td_api;

class TdConnection {
public:
    struct TdlibCredentials {
        std::int32_t api_id{0};
        std::string api_hash;
    };

    TdConnection(std::unique_ptr<ExternalLogger> externalLogger, TdlibCredentials credentials);

    void start(std::string database_dir, std::string files_dir, std::string ca_path);

    void fetch_ips(int size);

    ConnectionTestResult check_connection(int timeout_ms);

    void quit();

private:
    using Object = td_api::object_ptr<td_api::Object>;
    using Clock = std::chrono::steady_clock;

    std::unique_ptr<ExternalLogger> externalLogger;

    std::vector<td::IPAddress> ip_addresses;

    std::mutex ips_mutex_;
    std::condition_variable ips_cv_;

    bool ips_ready_{false};
    bool fetch_in_progress_{false};

    std::string database_dir_;
    std::string files_dir_;
    std::string ca_path_;
    TdlibCredentials tdlib_credentials_;

    std::unique_ptr<td::ClientManager> client_manager;
    td::ClientManager::ClientId client_id;
    std::uint32_t request_id = 0;

    std::map<uint32_t, std::function<void(Object)>> handlers;
    std::thread* process_response_thread;
    bool is_process{true};

    std::mutex state_mutex_;
    std::unordered_map<std::int32_t, std::uint64_t> state_counters_;
    std::unordered_map<std::int32_t, std::uint64_t> get_state_counters_snapshot();

    double test_proxy();

    void fetch_dc_updates();

    std::vector<td::IPAddress> lowest_latency_ips(int size);

    Record measure_rtt(td::IPAddress& ip, int count, std::chrono::steady_clock::time_point deadline);

    bool measure_tcp_rtt(
        const td::IPAddress& ip,
        int timeout_ms,
        double& rtt_ms
    );

    bool measure_media_load(
      const std::string& post_url,
      int timeout_ms,
      int& duration_ms
    );

    void send_request(td_api::object_ptr<td_api::Function> request);

    bool send_request_sync(
        td_api::object_ptr<td_api::Function> request,
        Object& result
    );

    void process_responses();

};
