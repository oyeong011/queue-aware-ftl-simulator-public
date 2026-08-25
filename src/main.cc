#include <iostream>
#include <string>

#include "ftlsim/Config.h"
#include "ftlsim/Ftl.h"
#include "ftlsim/Trace.h"

using namespace ftlsim;

namespace {
std::string argOr(int argc, char** argv, const std::string& flag, const std::string& def) {
  for (int i = 1; i < argc - 1; i++) {
    if (flag == argv[i]) return argv[i + 1];
  }
  return def;
}
}  // namespace

int main(int argc, char** argv) {
  std::string nand_cfg = argOr(argc, argv, "--nand", "configs/nand.conf");
  std::string ftl_cfg = argOr(argc, argv, "--ftl", "configs/ftl.conf");
  std::string trace_path = argOr(argc, argv, "--trace", "");
  std::string policy_override = argOr(argc, argv, "--policy", "");
  std::string qd_override = argOr(argc, argv, "--qd", "");
  std::string op_override = argOr(argc, argv, "--op", "");
  std::string label = argOr(argc, argv, "--label", "run");

  if (trace_path.empty()) {
    std::cerr << "usage: ftlsim --trace <csv> [--nand nand.conf] [--ftl ftl.conf] "
                 "[--policy foreground|fixed_background|queue_aware] [--qd N] "
                 "[--op over_provisioning_pct] [--label name]\n";
    return 1;
  }

  Config ncfg = Config::load(nand_cfg);
  Config fcfg = Config::load(ftl_cfg);
  NandGeometry geo = NandGeometry::fromConfig(ncfg);
  FtlParams params = FtlParams::fromConfig(fcfg);
  if (!policy_override.empty()) params.gc_policy = parseGcPolicy(policy_override);
  if (!qd_override.empty()) params.queue_depth = std::stoul(qd_override);
  if (!op_override.empty()) params.over_provisioning_pct = std::stod(op_override);

  auto trace = loadTrace(trace_path, geo.page_size_bytes);

  Ftl ftl(geo, params);
  RunMetrics m = ftl.run(trace);

  // Single CSV row (header + data) to stdout — callers/scripts append/aggregate.
  std::cout << "label,request_count,completed_request_count,simulated_time_ns,"
                "throughput_reqs_per_sec,avg_latency_ns,p50_latency_ns,p95_latency_ns,"
                "p99_latency_ns,avg_queue_wait_ns,avg_device_service_ns,host_write_pages,"
                "nand_write_pages,waf,gc_count,foreground_gc_count,background_gc_count,"
                "pages_moved_by_gc,gc_induced_stall_ns,free_page_ratio_end,"
                "invalid_page_ratio_end,erase_count_mean,erase_count_stddev,erase_count_max\n";
  std::cout << label << "," << m.request_count << "," << m.completed_request_count << ","
            << m.simulated_time_ns << "," << m.throughput_reqs_per_sec << "," << m.avg_latency_ns
            << "," << m.p50_latency_ns << "," << m.p95_latency_ns << "," << m.p99_latency_ns
            << "," << m.avg_queue_wait_ns << "," << m.avg_device_service_ns << ","
            << m.host_write_pages << "," << m.nand_write_pages << "," << m.waf << ","
            << m.gc_count << "," << m.foreground_gc_count << "," << m.background_gc_count << ","
            << m.pages_moved_by_gc << "," << m.gc_induced_stall_ns << ","
            << m.free_page_ratio_end << "," << m.invalid_page_ratio_end << ","
            << m.erase_count_mean << "," << m.erase_count_stddev << "," << m.erase_count_max
            << "\n";
  return 0;
}
