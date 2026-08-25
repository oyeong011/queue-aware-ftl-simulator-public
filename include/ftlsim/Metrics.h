#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ftlsim {

struct RunMetrics {
  uint64_t request_count = 0;
  uint64_t completed_request_count = 0;
  uint64_t simulated_time_ns = 0;

  double throughput_reqs_per_sec = 0.0;
  double avg_latency_ns = 0.0;
  double p50_latency_ns = 0.0;
  double p95_latency_ns = 0.0;
  double p99_latency_ns = 0.0;
  double avg_queue_wait_ns = 0.0;
  double avg_device_service_ns = 0.0;

  uint64_t host_write_pages = 0;
  uint64_t nand_write_pages = 0;
  double waf = 0.0;  // nand_write_pages / host_write_pages

  uint64_t gc_count = 0;
  uint64_t foreground_gc_count = 0;
  uint64_t background_gc_count = 0;
  uint64_t pages_moved_by_gc = 0;
  uint64_t gc_induced_stall_ns = 0;

  double free_page_ratio_end = 0.0;
  double invalid_page_ratio_end = 0.0;

  double erase_count_mean = 0.0;
  double erase_count_stddev = 0.0;
  uint32_t erase_count_max = 0;

  // Raw per-request latencies, kept for percentile computation and for
  // free-page-ratio-over-time / GC timeline figures.
  std::vector<double> latencies_ns;

  static double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p * (v.size() - 1));
    return v[idx];
  }

  void finalizeLatencyStats() {
    if (latencies_ns.empty()) return;
    double sum = 0;
    for (double l : latencies_ns) sum += l;
    avg_latency_ns = sum / latencies_ns.size();
    p50_latency_ns = percentile(latencies_ns, 0.50);
    p95_latency_ns = percentile(latencies_ns, 0.95);
    p99_latency_ns = percentile(latencies_ns, 0.99);
  }

  static void eraseStats(const std::vector<uint32_t>& erase_counts, double& mean,
                          double& stddev, uint32_t& max_v) {
    if (erase_counts.empty()) {
      mean = stddev = 0;
      max_v = 0;
      return;
    }
    double sum = 0;
    max_v = 0;
    for (auto e : erase_counts) {
      sum += e;
      max_v = std::max(max_v, e);
    }
    mean = sum / erase_counts.size();
    double sq = 0;
    for (auto e : erase_counts) sq += (e - mean) * (e - mean);
    stddev = std::sqrt(sq / erase_counts.size());
  }
};

}  // namespace ftlsim
