#include "TdConnection.h"
#include "Record.h"
#include "td/utils/Promise.h"
#include "StatsUtils.h"
#include "ConnectionTestResult.h"
#include "td/utils/logging.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <curl/curl.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

static const std::string media_urls[] = {
    "https://t.me/durov/466?embed=1&mode=tme",
    "https://t.me/durov/419?embed=1&mode=tme",
    "https://t.me/d_code/24961?embed=1&mode=tme",
    "https://t.me/durov/404?embed=1&mode=tme",
    "https://t.me/durov/358?embed=1&mode=tme"
};

TdConnection::TdConnection(
    std::unique_ptr<ExternalLogger> logger,
    TdConnection::TdlibCredentials credentials
)
    : client_manager(std::make_unique<td::ClientManager>()),
      externalLogger(std::move(logger)),
      tdlib_credentials_(std::move(credentials)),
      client_id(client_manager->create_client_id()) {
    td::ClientManager::execute(
        td_api::make_object<td_api::setLogVerbosityLevel>(1)
    );
}

void TdConnection::start(std::string database_dir, std::string files_dir, std::string ca_path) {
    LOG(INFO) << "Start connection manager";
    database_dir_ = database_dir;
    files_dir_ = files_dir;
    ca_path_ = ca_path;
    process_response_thread = new std::thread([this] {
        process_responses();
    });
}

void TdConnection::fetch_ips(int size) {
    LOG(INFO) << "Fetching actual Telegram DC IP addresses";
    externalLogger->log(2, "Fetching actual Telegram DC IP addresses...");
    std::lock_guard<std::mutex> lock(ips_mutex_);

    if (fetch_in_progress_) {
        return;
    }

    fetch_in_progress_ = true;
    ips_ready_ = false;

    auto promise = td::PromiseCreator::lambda(
        [this](td::Result<std::vector<td::IPAddress>> result) {
            {
                std::lock_guard<std::mutex> lock(ips_mutex_);
                fetch_in_progress_ = false;

                if (result.is_ok()) {
                    ip_addresses = result.move_as_ok();
                    ips_ready_ = true;
                    externalLogger->log(2, "Telegram DC IP addresses were fetched successfuly");
                } else {
                    ips_ready_ = false;
                    externalLogger->log(1, "Telegram DC IP addresses were fetched with failure");
                }
            }
            ips_cv_.notify_all();
        }
    );

    std::thread([this, size, promise = promise.release()]() mutable {
        test_proxy();
        auto ips = lowest_latency_ips(size);
        promise->set_value(std::move(ips));
    }).detach();
}

ConnectionTestResult TdConnection::check_connection(int timeout_ms) {
    LOG(INFO) << "Measuring telegram connection";
    externalLogger->log(2, "Measuring telegram connection...");
    if (!process_response_thread) {
        LOG(ERROR) << "Internal error";
        externalLogger->log(0, "Internal error. process_response_thread did not start");
        return ConnectionTestResult::error(
            "Internal error"
        );
    }

    externalLogger->log(2, "Waiting for Telegram DC IPs...");
    auto start = Clock::now();
    auto deadline = start + std::chrono::milliseconds(timeout_ms);

    std::unique_lock<std::mutex> lock(ips_mutex_);

    if (!fetch_in_progress_ && !ips_ready_) {
        lock.unlock();
        fetch_ips(5);
        lock.lock();
    }

    bool ready = ips_cv_.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this] { return ips_ready_ || !fetch_in_progress_; }
    );

    if (!ready || !ips_ready_) {
        LOG(ERROR) << "Timeout waiting for IPs";
        externalLogger->log(0, "Timeout waiting for IPs");
        return ConnectionTestResult::error(
            "Timeout waiting for IPs"
        );
    }

    externalLogger->log(2, "Telegram DC IP addresses found!");

    auto addresses = ip_addresses;
    lock.unlock();

    std::vector<double> seconds;
    std::vector<Record> records;

    auto addresses_it = addresses.begin();
    int test_count = 5;

    std::unique_lock<std::mutex> state_lock(state_mutex_);
    state_counters_.clear();
    state_lock.unlock();

    while(true) {
        auto now = Clock::now();
        if (now >= deadline) {
            externalLogger->log(0, "Reach deadline while testing connection");
            LOG(ERROR) << "Reach deadline while testing connection";
            break;
        }

        if (test_count-- < 0) {
            break;
        }

        auto sec = test_proxy();
        seconds.push_back(sec);

        if (addresses_it != addresses.end()) {
            auto record = measure_rtt(*addresses_it, 20, deadline);
            records.push_back(record);
            ++addresses_it;
        }
    }

    if (records.size() < 3) {
        externalLogger->log(2, "No reachable Telegram DCs. [" + std::to_string(records.size()) + "]");
        LOG(ERROR) << "No reachable Telegram DCs. [" << records.size() << "]";
        return ConnectionTestResult::error(
            "No reachable Telegram DCs"
        );
    }

    if (seconds.size() < 5) {
        externalLogger->log(2, "Failed to use MTProto methods. [" + std::to_string(seconds.size()) + "]");
        LOG(ERROR) << "Failed to use MTProto methods. [" << seconds.size() << "]";
        return ConnectionTestResult::error(
            "Failed to use MTProto methods"
        );
    }

    TestProxyResult test_proxy_res(seconds);

    if (test_proxy_res.success_rate() < 0.2) {
        LOG(ERROR) << "MTProto testProxy failed";
        externalLogger->log(2, "MTProto testProxy failed");
        return ConnectionTestResult::error(
            "MTProto testProxy failed"
        );
    }

    // ---------------------------
    // Measure media load (HTML -> parse -> media download) 5 times
    // ---------------------------
    externalLogger->log(2, "Measuring Telegram media load (5 times)...");
    std::vector<double> media_mss;
    int media_attempts = (int)(sizeof(media_urls) / sizeof(media_urls[0]));

    for (int i = 0; i < media_attempts; ++i) {
        auto now = Clock::now();
        if (now >= deadline) {
            externalLogger->log(1, "Deadline reached while measuring media load");
            break;
        }

        int media_ms = -1;

        int per_try_timeout_ms = 2000;
        auto remaining_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        per_try_timeout_ms = std::min(per_try_timeout_ms, std::max(500, remaining_ms));

        if (measure_media_load(media_urls[i], per_try_timeout_ms, media_ms) && media_ms > 0) {
            media_mss.push_back((double)media_ms);
        }
    }

    double media_success_rate = (media_attempts > 0) ? ((double)media_mss.size() / (double)media_attempts) : 0.0;
    double media_p50 = media_mss.empty() ? 0.0 : stats::median(media_mss);
    double media_p95 = media_mss.empty() ? 0.0 : stats::percentile(media_mss, 0.95);

    double media_lat  = stats::lerp(media_p50, 150, 2000);
    double media_tail = (media_mss.size() >= 2) ? stats::tail_rate(media_p50, media_p95) : 0.0;
    double media_rel  = 1.0 - stats::lerp(media_success_rate, 0.60, 0.95);

    double bad_media = 0.55 * media_lat + 0.20 * media_tail + 0.25 * media_rel;
    double score_media = 100 * (1 - bad_media);

    if (media_mss.empty()) {
        score_media = 20.0;
    }

    externalLogger->log(
        2,
        "Media summary: p50=" + std::to_string((int)media_p50) + "ms, p95=" + std::to_string((int)media_p95) +
        "ms, success=" + std::to_string(media_success_rate)
    );

    int reconnection_count = 0;
    auto snapshot = get_state_counters_snapshot();
    for (auto it = snapshot.begin(); it != snapshot.end(); ++it) {
        if (it->first == td_api::connectionStateConnecting::ID || it->first == td_api::connectionStateWaitingForNetwork::ID) {
            reconnection_count += it->second;
        }
    }

    auto duration = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    // Score test proxy
    double tp_lat = stats::lerp(test_proxy_res.p50(), 30, 300);
    double tp_tail = stats::tail_rate(test_proxy_res.p50(), test_proxy_res.p95());
    double tp_jit = stats::jitter_rate(test_proxy_res.jitter(), test_proxy_res.p50());
    double tp_rel = 1.0 - stats::lerp(test_proxy_res.success_rate(), 0.75, 0.95);
    double bad_tp = 0.35 * tp_lat + 0.15 * tp_tail + 0.15 * tp_jit + 0.35 * tp_rel;
    double score_tp = 100 * (1 - bad_tp);

    // Score TCP RTT
    std::vector<double> dc_p50_v;
    std::vector<double> dc_p95_v;
    std::vector<double> dc_jitter_v;
    std::vector<double> dc_success_v;
    for (auto& record: records) {
        dc_p50_v.push_back(record.p50());
        dc_p95_v.push_back(record.p95());
        dc_jitter_v.push_back(record.jitter());
        dc_success_v.push_back(record.success_rate());
    }
    double tcp_p50 = stats::median(dc_p50_v);
    double tcp_p95 = stats::median(dc_p95_v);
    double tcp_jitter = stats::median(dc_jitter_v);
    double tcp_success_rate = stats::median(dc_success_v);

    double tcp_lat = stats::lerp(tcp_p50, 20, 200);
    double tcp_tail = stats::tail_rate(tcp_p50, tcp_p95);
    double tcp_jit_rate = stats::jitter_rate(tcp_jitter, tcp_p50);
    double tcp_rel = 1.0 - stats::lerp(tcp_success_rate, 0.75, 0.95);
    double bad_tcp = 0.40 * tcp_lat + 0.15 * tcp_tail + 0.15 * tcp_jit_rate + 0.30 * tcp_rel;
    double score_tcp = 100 * (1 - bad_tcp);

    // Score reconnections
    double reconnect_rate = stats::lerp(reconnection_count / (duration / 1000), 0.01, 0.10);

    double base_score = 0.50 * score_tp + 0.30 * score_tcp + 0.30 * score_media;
    double final_score = stats::clamp(base_score * (1 - 0.60 * reconnect_rate), 0, 100);

    ConnectionTestResult result {
        final_score,
        score_tp,
        score_tcp,
        score_media,
        std::move(records),
        std::move(test_proxy_res),
        snapshot
    };

    externalLogger->log(2, "Finish analysing connection");

    LOG(INFO) << result.summary();

    return result;
}

std::unordered_map<std::int32_t, std::uint64_t> TdConnection::get_state_counters_snapshot() {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    return state_counters_;
}

void TdConnection::fetch_dc_updates() {
    ip_addresses = client_manager->fetch_dc_updates();
}

std::vector<td::IPAddress> TdConnection::lowest_latency_ips(int size) {
    fetch_dc_updates();

    if (size <= 0) return {};

    std::vector<Record> records;

    bool is_ip6_available{false};
    for (const td::IPAddress& ip : ip_addresses) {
        if (!ip.is_ipv6()) continue;
        double rtt;
        is_ip6_available = measure_tcp_rtt(ip, 500, rtt);
        if (is_ip6_available) break;
    }

    std::vector<td::IPAddress> checked_addresses{};
    for (const td::IPAddress& ip : ip_addresses) {
        if (ip.is_ipv6() && !is_ip6_available) continue;
        if (std::find(checked_addresses.begin(),checked_addresses.end(), ip) != checked_addresses.end()) continue;

        std::vector<double> rtt_mss;
        for (int count = 0; count < 4; ++count) {
            double rtt_ms;
            if (measure_tcp_rtt(ip, 500, rtt_ms)) {
                rtt_mss.push_back(rtt_ms);
            }
        }

        checked_addresses.push_back(ip);

        if (rtt_mss.size() < 2) {
            continue;
        }

        Record record{rtt_mss, ip, 4, (int) rtt_mss.size()};
        records.push_back(record);
    }

    std::sort(records.begin(), records.end(), [](Record const& lhs, Record const& rhs) {
        return lhs.p50() < rhs.p50();
    });

    std::vector<td::IPAddress> output;
    for (const Record& record : records) {
        if ((int)output.size() >= size) break;
        output.push_back(record.ip_address());
    }

    return output;
}

Record TdConnection::measure_rtt(td::IPAddress& ip, int count, std::chrono::steady_clock::time_point deadline) {
    std::vector<double> rtt_s{};
    int i = 0;
    for (i = 0; i < count; ++i) {
        auto now = Clock::now();
        if (now >= deadline) break;

        double rtt;
        if (measure_tcp_rtt(ip, 1000, rtt)) {
            rtt_s.push_back(rtt);
        }
    }
    Record record{rtt_s, ip, i, (int) rtt_s.size()};

    return record;
}

bool TdConnection::measure_tcp_rtt(
    const td::IPAddress& ip,
    int timeout_ms,
    double& rtt_ms
) {
    if (!ip.is_valid()) {
        return false;
    }

    int sock = socket(
        ip.get_address_family(),
        SOCK_STREAM,
        IPPROTO_TCP
    );
    if (sock < 0) {
        return false;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }

    auto start = Clock::now();
    int res = connect(
        sock,
        ip.get_sockaddr(),
        static_cast<socklen_t>(ip.get_sockaddr_len())
    );

    if (res < 0 && errno != EINPROGRESS) {
        close(sock);
        return false;
    }

    pollfd pfd{};
    pfd.fd = sock;
    pfd.events = POLLOUT;

    int poll_res = poll(&pfd, 1, timeout_ms);
    if (poll_res <= 0) {
        LOG(ERROR) << "Failed to poll ip address [" << ip.get_ip_str().str().c_str() << "]" << ". Error code = " << poll_res;
        close(sock);
        return false;
    }

    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);

    auto end = Clock::now();
    close(sock);

    if (err != 0) {
        LOG(ERROR) << "Failed to getsockopt of ip address [" << ip.get_ip_str().str().c_str() << "]" << ". Error code = " << poll_res;
        return false;
    }

    rtt_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return true;
}

void TdConnection::quit() {
    is_process = false;
    if (process_response_thread) {
        process_response_thread->join();
        delete process_response_thread;
        process_response_thread = nullptr;
    }
}

double TdConnection::test_proxy() {
    TdConnection::Object obj;
    if (send_request_sync(td_api::make_object<td_api::pingProxy>(0), obj)) {
        double seconds = td::move_tl_object_as<td_api::seconds>(obj)->seconds_;
        return seconds;
    }

    return -1;
}

void TdConnection::send_request(
    td_api::object_ptr<td_api::Function> request
) {
    auto id = ++request_id;
    client_manager->send(client_id, id, std::move(request));
}

void TdConnection::process_responses() {
    while (is_process) {
        auto response = client_manager->receive(10.0);
        if (!response.object) continue;

        auto it = handlers.find(response.request_id);
        if (it != handlers.end()) {
            it->second(std::move(response.object));
            handlers.erase(it);
            continue;
        }

        td_api::downcast_call(
            *response.object,
            overloaded(
                [this](td_api::updateAuthorizationState& update) {
                    td_api::downcast_call(
                        *update.authorization_state_,
                        overloaded(
                            [this](td_api::authorizationStateWaitTdlibParameters&) {
                                LOG(INFO) << "Authorising with tdlib params";
                                if (tdlib_credentials_.api_id <= 0 || tdlib_credentials_.api_hash.empty()) {
                                    LOG(ERROR) << "TDLib credentials are not set";
                                    externalLogger->log(1, "TDLib credentials are not set");
                                    return;
                                }

                                auto request = td_api::make_object<td_api::setTdlibParameters>();

                                request->database_directory_ = database_dir_;
                                request->files_directory_ = files_dir_;
                                request->use_message_database_ = true;
                                request->use_secret_chats_ = true;
                                request->api_id_ = tdlib_credentials_.api_id;
                                request->api_hash_ = tdlib_credentials_.api_hash;
                                request->system_language_code_ = "en";
                                request->device_model_ = "Mobile";
                                request->application_version_ = "1.0";

                                send_request(std::move(request));
                            },
                            [](auto&) {}
                        )
                    );
                },
                [this](td_api::updateConnectionState& update) {
                    if (!update.state_) return;
                    LOG(INFO) << "Connection state has changed [" << update.state_->get_id() << "]";
                    externalLogger->log(2, "Connection state has changed [" + std::to_string(update.state_->get_id()) + "]");
                    auto id = update.state_->get_id();
                    std::lock_guard<std::mutex> state_lock(state_mutex_);
                    state_counters_[id]++;
                },
                [this](td_api::error& error) {
                    LOG(ERROR) << "Error [" << error.code_ << "; " << error.message_.c_str() << "]";
                    externalLogger->log(1, "Error [" + std::to_string(error.code_) + "; " + error.message_.c_str() + "]");
                },
                [](auto&) {}
            )
        );
    }
}

bool TdConnection::send_request_sync(
    td_api::object_ptr<td_api::Function> request,
    TdConnection::Object& result
) {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool success = false;

    auto id = ++request_id;
    handlers.emplace(id, [&](Object obj) {
        {
            std::lock_guard<std::mutex> lock(m);
            if (obj->get_id() != td_api::error::ID) {
                result = std::move(obj);
                success = true;
            }
            done = true;
        }
        cv.notify_one();
    });

    client_manager->send(client_id, id, std::move(request));

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&] { return done; });

    return success;
}

/** Measuring media download */

static size_t CurlWriteToString(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<const char*>(contents), total);
    return total;
}

static size_t CurlDiscard(void* contents, size_t size, size_t nmemb, void* userp) {
    (void)contents;
    (void)userp;
    return size * nmemb;
}

static bool extract_photo_wrap_href(const std::string& html, std::string& out_href) {
    const std::string cls = "tgme_widget_message_photo_wrap";
    size_t pos = html.find(cls);
    if (pos == std::string::npos) return false;

    size_t a_pos = html.rfind("<a", pos);
    if (a_pos == std::string::npos) return false;

    size_t tag_end = html.find('>', a_pos);
    if (tag_end == std::string::npos) return false;

    std::string a_tag = html.substr(a_pos, tag_end - a_pos + 1);

    const std::string href_key = "href=\"";
    size_t href_pos = a_tag.find(href_key);
    if (href_pos == std::string::npos) return false;

    href_pos += href_key.size();
    size_t href_end = a_tag.find('"', href_pos);
    if (href_end == std::string::npos) return false;

    out_href = a_tag.substr(href_pos, href_end - href_pos);
    return !out_href.empty();
}


bool TdConnection::measure_media_load(
    const std::string& post_url,
    int timeout_ms,
    int& duration_ms
) {
    duration_ms = -1;

    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG(ERROR) << "curl_easy_init failed";
        return false;
    }

    auto cleanup = [&]() {
        curl_easy_cleanup(curl);
    };

    // --- 1) Download HTML ---
    std::string html;

    curl_easy_setopt(curl, CURLOPT_URL, post_url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);

    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Linux; Android 12; Mobile) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36");

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);

    // Timeouts
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)std::min(timeout_ms, 10'000));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);

    // HTTPS verify
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (!ca_path_.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path_.c_str());
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG(ERROR) << "Failed to fetch HTML: " << curl_easy_strerror(res);
        cleanup();
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        LOG(ERROR) << "HTML HTTP code: " << http_code;
        cleanup();
        return false;
    }

    // --- 2) Parse media href ---
    std::string href;
    if (!extract_photo_wrap_href(html, href)) {
        LOG(ERROR) << "tgme_widget_message_photo_wrap href not found";
        cleanup();
        return false;
    }

    std::string media_url = href;

    // --- 3) Download media and measure duration ---
    curl_easy_setopt(curl, CURLOPT_URL, media_url.c_str());

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlDiscard);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, nullptr);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);

    auto start = Clock::now();
    res = curl_easy_perform(curl);
    auto end = Clock::now();

    if (res != CURLE_OK) {
        LOG(ERROR) << "Failed to download media: " << curl_easy_strerror(res);
        cleanup();
        return false;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        LOG(ERROR) << "Media HTTP code: " << http_code;
        cleanup();
        return false;
    }

    duration_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    double dl_bytes = 0.0;
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &dl_bytes);
    LOG(INFO) << "Media downloaded: " << (long long)dl_bytes << " bytes in " << duration_ms << " ms";

    cleanup();
    return true;
}
