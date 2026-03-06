#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Record.h"
#include "TestProxyResult.h"

struct ConnectionTestResult {

  // === status ===
  bool is_error;
  std::string error_message;

  // === scores ===
  double final_score;
  double tp_score;
  double tcp_score;
  double media_score;

  // === raw data ===
  std::vector<Record> tcp_records;
  TestProxyResult test_proxy_res;
  std::unordered_map<std::int32_t, std::uint64_t> state_counters;

  // Normal (successful) result
  ConnectionTestResult(
      double final_score_,
      double tp_score_,
      double tcp_score_,
      double media_score_,
      std::vector<Record> tcp_records_,
      TestProxyResult test_proxy_res_,
      std::unordered_map<std::int32_t, std::uint64_t> state_counters_
  );

  // Error result factory
  static ConnectionTestResult error(std::string message);

  /// Human-readable explanation
  std::string summary() const;

private:
  static const char* quality_label(double score);
  static const char* connection_state_name(std::int32_t id);

  ConnectionTestResult(bool is_error_, std::string message);

};
