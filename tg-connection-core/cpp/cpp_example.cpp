#include "../connection/TdConnection.h"
#include "../connection/ConnectionTestResult.h"

#include <iostream>
#include <sstream>
#include <memory>
#include <fstream>
#include <unordered_map>
#include <curl/curl.h>

class ConsoleLogger : public ExternalLogger {
public:
    void log(int level, const std::string& message) override {
        std::cout << "[LOG-" << level << "] " << message << std::endl;
    }
};

#ifndef TD_CONNECTION_TEST_CONFIG_DIR
#define TD_CONNECTION_TEST_CONFIG_DIR "."
#endif

static std::string trim(std::string s) {
    const char* ws = " \t\r\n";
    const size_t begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return "";
    }
    const size_t end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

static bool load_tdlib_credentials(
    const std::string& path,
    TdConnection::TdlibCredentials& creds,
    std::string& error
) {
    std::ifstream input(path);
    if (!input.is_open()) {
        error = "Can't open credentials file: " + path;
        return false;
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, sep));
        std::string value = trim(line.substr(sep + 1));
        if (!value.empty() && value.front() == '"' && value.back() == '"' && value.size() >= 2) {
            value = value.substr(1, value.size() - 2);
        }
        values[key] = value;
    }

    auto api_id_it = values.find("API_ID");
    auto api_hash_it = values.find("API_HASH");
    if (api_id_it == values.end() || api_hash_it == values.end()) {
        error = "Credentials file must contain API_ID and API_HASH";
        return false;
    }

    try {
        creds.api_id = std::stoi(api_id_it->second);
    } catch (...) {
        error = "API_ID must be a valid integer";
        return false;
    }

    creds.api_hash = api_hash_it->second;
    if (creds.api_id <= 0 || creds.api_hash.empty()) {
        error = "API_ID must be > 0 and API_HASH must be non-empty";
        return false;
    }

    return true;
}

int main() {
    // Initialize curl (required for measure_media_load)
    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::cout << "Starting Telegram Connection Tester..." << std::endl;

    // Create logger
    auto logger = std::make_unique<ConsoleLogger>();

    TdConnection::TdlibCredentials credentials;
    std::string credentials_error;
    const std::string credentials_path = std::string(TD_CONNECTION_TEST_CONFIG_DIR) + "/tdlib_credentials.env";
    if (!load_tdlib_credentials(credentials_path, credentials, credentials_error)) {
        std::cerr << credentials_error << std::endl;
        std::cerr << "Create file " << credentials_path << " with API_ID and API_HASH." << std::endl;
        curl_global_cleanup();
        return 1;
    }

    // Create connection
    TdConnection connection(std::move(logger), std::move(credentials));

    // TDLib directories (can be changed for Android/desktop)
    std::string database_dir = "./td_db";
    std::string files_dir    = "./td_files";

    connection.start(database_dir, files_dir, "");

    while (true) {
        std::cout << "\nEnter command (c - check, q - quit): ";
        std::string line;
        std::getline(std::cin, line);

        std::istringstream ss(line);
        std::string action;
        if (!(ss >> action)) {
            continue;
        }

        if (action == "c") {
            std::cout << "Starting Telegram connection test..." << std::endl;

            auto result = connection.check_connection(60000);

            std::cout << "Testing complete\n";
            std::cout << result.summary() << std::endl;
        }
        else if (action == "q") {
            std::cout << "Quitting connection test..." << std::endl;
            connection.quit();
            break;
        }
        else {
            std::cout << "Unknown command. Try again." << std::endl;
        }
    }

    curl_global_cleanup();

    std::cout << "Testing completed." << std::endl;

    return 0;
}
