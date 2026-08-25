// Invariant / correctness checks for the FTL simulator core.
// No test framework — plain assert-style checks, one function per invariant,
// nonzero exit on first failure. Run via `ctest --test-dir build --output-on-failure`.
#include <cassert>
#include <cstdio>
#include <vector>

#include "ftlsim/Ftl.h"

using namespace ftlsim;

namespace {

NandGeometry smallGeo() {
  NandGeometry g;
  g.page_size_bytes = 4096;
  g.pages_per_block = 4;
  g.blocks_per_die = 4;
  g.dies_per_channel = 1;
  g.channels = 1;
  g.t_read_ns = 50000;
  g.t_program_ns = 600000;
  g.t_erase_ns = 3000000;
  return g;
}

FtlParams smallParams(GcPolicy policy) {
  FtlParams p;
  p.over_provisioning_pct = 25.0;  // 16 total pages -> capacity 12
  p.critical_free_ratio = 0.10;
  p.background_free_ratio = 0.30;
  p.low_queue_threshold = 1;
  p.gc_policy = policy;
  p.queue_depth = 4;
  return p;
}

Request wr(uint64_t t, uint64_t lba) { return {t, Op::WRITE, lba, 1}; }
Request rd(uint64_t t, uint64_t lba) { return {t, Op::READ, lba, 1}; }

int failures = 0;
#define CHECK(cond, name)                                            \
  do {                                                                \
    if (cond) {                                                       \
      std::printf("[PASS] %s\n", name);                               \
    } else {                                                          \
      std::printf("[FAIL] %s\n", name);                               \
      failures++;                                                     \
    }                                                                 \
  } while (0)

// 1,2,3: no double-mapping, invalid pages never counted valid, page counts sum to total.
void test_mapping_and_page_accounting() {
  Ftl ftl(smallGeo(), smallParams(GcPolicy::QUEUE_AWARE));
  std::vector<Request> trace;
  uint64_t t = 0;
  for (int round = 0; round < 5; round++)
    for (uint64_t lba = 0; lba < 8; lba++) trace.push_back(wr(t++, lba));
  ftl.run(trace);
  std::string err;
  CHECK(ftl.validateInvariants(&err), "mapping bijection + free/valid/invalid accounting");
  if (!err.empty()) std::printf("       %s\n", err.c_str());
}

// 4: overwrite invalidates the old physical page.
void test_overwrite_invalidates_old_page() {
  Ftl ftl(smallGeo(), smallParams(GcPolicy::QUEUE_AWARE));
  std::vector<Request> trace = {wr(0, 0), wr(1, 0)};
  ftl.run(trace);
  int64_t p_final = ftl.physicalOf(0);
  CHECK(p_final >= 0, "overwrite: LPN still mapped after 2nd write");
  std::string err;
  CHECK(ftl.validateInvariants(&err), "overwrite: invariants hold (old page now INVALID, not VALID)");
}

// 5: GC preserves data that was valid before it ran.
void test_gc_preserves_valid_data() {
  Ftl ftl(smallGeo(), smallParams(GcPolicy::QUEUE_AWARE));
  std::vector<Request> trace;
  trace.push_back(wr(0, 11));  // written once, never touched again
  uint64_t t = 1;
  for (int round = 0; round < 30; round++)
    for (uint64_t lba = 0; lba < 8; lba++) trace.push_back(wr(t++, lba));
  ftl.run(trace);
  CHECK(ftl.physicalOf(11) >= 0, "GC: untouched LPN 11 still mapped after heavy GC pressure");
  std::string err;
  CHECK(ftl.validateInvariants(&err), "GC: invariants hold after GC");
}

// 6: full sequential write over logical capacity maps every LPN exactly once.
void test_full_sequential_write_mapping_count() {
  Ftl ftl(smallGeo(), smallParams(GcPolicy::QUEUE_AWARE));
  std::vector<Request> trace;
  uint64_t cap = ftl.logicalCapacityPages();
  for (uint64_t lba = 0; lba < cap; lba++) trace.push_back(wr(lba, lba));
  RunMetrics m = ftl.run(trace);
  uint64_t mapped = 0;
  for (uint64_t lba = 0; lba < cap; lba++) mapped += (ftl.physicalOf(lba) >= 0);
  CHECK(mapped == cap, "full sequential write: every logical page mapped exactly once");
  CHECK(m.host_write_pages == cap, "full sequential write: host_write_pages == capacity");
}

// 7: no overwrite / no GC pressure -> WAF ~= 1.
void test_waf_near_one_without_gc() {
  Ftl ftl(smallGeo(), smallParams(GcPolicy::QUEUE_AWARE));
  std::vector<Request> trace;
  uint64_t cap = ftl.logicalCapacityPages();
  for (uint64_t lba = 0; lba < cap; lba++) trace.push_back(wr(lba, lba));
  RunMetrics m = ftl.run(trace);
  CHECK(m.waf > 0.999 && m.waf < 1.001, "WAF == 1.0 with no overwrites, no GC copies");
}

// 8: once GC runs, NAND writes are never fewer than host writes.
void test_gc_nand_writes_gte_host_writes() {
  Ftl ftl(smallGeo(), smallParams(GcPolicy::QUEUE_AWARE));
  std::vector<Request> trace;
  uint64_t t = 0;
  for (int round = 0; round < 30; round++)
    for (uint64_t lba = 0; lba < 8; lba++) trace.push_back(wr(t++, lba));
  RunMetrics m = ftl.run(trace);
  CHECK(m.gc_count > 0, "GC pressure test: GC actually triggered");
  CHECK(m.nand_write_pages >= m.host_write_pages, "GC: nand_write_pages >= host_write_pages");
}

// 9: identical config + trace -> byte-identical metrics (determinism).
void test_deterministic_reproducibility() {
  std::vector<Request> trace;
  uint64_t t = 0;
  for (int round = 0; round < 10; round++)
    for (uint64_t lba = 0; lba < 8; lba++) trace.push_back(wr(t++, lba));

  Ftl ftl1(smallGeo(), smallParams(GcPolicy::QUEUE_AWARE));
  RunMetrics m1 = ftl1.run(trace);
  Ftl ftl2(smallGeo(), smallParams(GcPolicy::QUEUE_AWARE));
  RunMetrics m2 = ftl2.run(trace);
  CHECK(m1.waf == m2.waf && m1.gc_count == m2.gc_count &&
            m1.nand_write_pages == m2.nand_write_pages &&
            m1.simulated_time_ns == m2.simulated_time_ns,
        "same config+trace -> identical metrics across runs");
}

// 10: queue_depth == 1 serializes requests (queue wait grows vs. deep queue).
// Needs >1 channel: with a single channel, device service time alone already
// serializes everything and the admission-window depth has nothing left to
// constrain, which would make this test vacuous.
void test_queue_depth_limits_outstanding() {
  NandGeometry geo = smallGeo();
  geo.channels = 4;
  geo.blocks_per_die = 4;

  std::vector<Request> trace;
  for (uint64_t i = 0; i < 8; i++) trace.push_back(wr(0, i));  // burst, all arrive at t=0

  FtlParams p1 = smallParams(GcPolicy::QUEUE_AWARE);
  p1.queue_depth = 1;
  Ftl ftl1(geo, p1);
  RunMetrics m1 = ftl1.run(trace);

  FtlParams pdeep = smallParams(GcPolicy::QUEUE_AWARE);
  pdeep.queue_depth = 64;
  Ftl ftl2(geo, pdeep);
  RunMetrics m2 = ftl2.run(trace);

  CHECK(m1.avg_queue_wait_ns > m2.avg_queue_wait_ns,
        "queue_depth=1 serializes requests (higher avg queue wait than deep queue)");
}

// 11: forced GC under critical free-page pressure completes without hanging.
void test_no_deadlock_under_critical_pressure() {
  Ftl ftl(smallGeo(), smallParams(GcPolicy::QUEUE_AWARE));
  std::vector<Request> trace;
  uint64_t t = 0;
  for (int round = 0; round < 100; round++)
    for (uint64_t lba = 0; lba < 8; lba++) trace.push_back(wr(t++, lba));
  bool completed = false;
  try {
    RunMetrics m = ftl.run(trace);
    completed = (m.completed_request_count == trace.size());
  } catch (const std::exception&) {
    completed = false;
  }
  CHECK(completed, "critical free-page pressure: run completes without hang/exception");
}

// 12: out-of-range LBA fails explicitly instead of silently no-opping.
void test_invalid_lba_fails_explicitly() {
  Ftl ftl(smallGeo(), smallParams(GcPolicy::QUEUE_AWARE));
  uint64_t cap = ftl.logicalCapacityPages();
  std::vector<Request> trace = {wr(0, cap + 100)};
  bool threw = false;
  try {
    ftl.run(trace);
  } catch (const InvalidLbaError&) {
    threw = true;
  }
  CHECK(threw, "out-of-range LBA raises InvalidLbaError");
}

}  // namespace

int main() {
  test_mapping_and_page_accounting();
  test_overwrite_invalidates_old_page();
  test_gc_preserves_valid_data();
  test_full_sequential_write_mapping_count();
  test_waf_near_one_without_gc();
  test_gc_nand_writes_gte_host_writes();
  test_deterministic_reproducibility();
  test_queue_depth_limits_outstanding();
  test_no_deadlock_under_critical_pressure();
  test_invalid_lba_fails_explicitly();

  std::printf("\n%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
