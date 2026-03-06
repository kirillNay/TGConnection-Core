
#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/ConnectionCreator.h"

namespace td {

class DcOptionsStore {

public:
    static DcOptionsStore& instance() {
        static DcOptionsStore store;
        return store;
    }

    void save_dc_options(DcOptions dc_options) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            dc_options = std::move(dc_options);
        }
        cv_.notify_all();
    }

    DcOptions get_dc_options() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dc_options;
    }

    std::vector<IPAddress> get_ip_addresses() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return extract_ip_addresses_locked();
    }

    std::vector<IPAddress> wait_for_ip_addresses(std::chrono::milliseconds timeout) const {
        std::unique_lock<std::mutex> lock(mutex_);

        auto has_ips = [&] {
            return !extract_ip_addresses_locked().empty();
        };

        if (timeout.count() <= 0) {
            cv_.wait(lock, has_ips);
        } else {
            cv_.wait_for(lock, timeout, has_ips);
        }

        return extract_ip_addresses_locked();
    }

private:
    DcOptionsStore() {
        dc_options = ConnectionCreator::get_default_dc_options(false);
    };

    std::vector<IPAddress> extract_ip_addresses_locked() const {
        std::vector<IPAddress> addresses;
        addresses.reserve(dc_options.dc_options.size());
        for (const auto& opt : dc_options.dc_options) {
            addresses.push_back(opt.get_ip_address());
        }
        return addresses;
    }

    ~DcOptionsStore() = default;
    DcOptionsStore(const DcOptionsStore&) = delete;
    DcOptionsStore& operator=(const DcOptionsStore&) = delete;

    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;

    DcOptions dc_options;
};

    
}