#pragma once
#include <cstdint>
#include <deque>
#include <vector>

#include "ftlsim/Config.h"
#include "ftlsim/Metrics.h"
#include "ftlsim/Types.h"

namespace ftlsim {

struct NandGeometry {
  uint32_t page_size_bytes;
  uint32_t pages_per_block;
  uint32_t blocks_per_die;
  uint32_t dies_per_channel;
  uint32_t channels;
  double t_read_ns;
  double t_program_ns;
  double t_erase_ns;

  uint64_t blocksTotal() const {
    return static_cast<uint64_t>(blocks_per_die) * dies_per_channel * channels;
  }
  uint64_t pagesTotal() const { return blocksTotal() * pages_per_block; }

  static NandGeometry fromConfig(const Config& c);
};

struct FtlParams {
  double over_provisioning_pct;
  double critical_free_ratio;
  double background_free_ratio;
  uint32_t low_queue_threshold;
  GcPolicy gc_policy;
  uint32_t queue_depth;

  static FtlParams fromConfig(const Config& c);
};

// Deterministic page-level FTL + NAND block model with three GC policies.
// One instance = one simulation run over one trace with fixed config/seed.
class Ftl {
 public:
  Ftl(NandGeometry geo, FtlParams params);

  // Runs the full trace and returns metrics. `outstanding_hint` is filled in
  // by the caller via processRequest's internal admission-window model.
  RunMetrics run(const std::vector<Request>& trace);

  // Cross-checks internal state consistency: free+valid+invalid page counts
  // match block bookkeeping, and l2p/p2l form a bijection on the valid-page
  // set. Used by tests/test_invariants.cc; not called during run() itself
  // (would slow down large sweeps).
  bool validateInvariants(std::string* err = nullptr) const;

  uint64_t freePages() const { return free_pages_; }
  uint64_t logicalCapacityPages() const { return logical_capacity_pages_; }
  // -1 if lpn was never written (or out of range).
  int64_t physicalOf(uint64_t lpn) const { return lpn < l2p_.size() ? l2p_[lpn] : -1; }

 private:
  struct Block {
    uint32_t channel;
    uint16_t erase_count = 0;
    uint16_t valid_count = 0;
    uint16_t invalid_count = 0;
    bool is_free = true;  // fully erased, unused
  };

  NandGeometry geo_;
  FtlParams params_;

  std::vector<Block> blocks_;
  std::vector<PageState> page_state_;
  std::vector<int64_t> l2p_;  // logical page number -> physical page id, -1 if unmapped
  std::vector<int64_t> p2l_;  // physical page id -> lpn, -1 if not valid
  std::vector<std::deque<uint32_t>> free_blocks_per_channel_;
  std::vector<uint32_t> active_block_per_channel_;
  std::vector<uint32_t> active_offset_per_channel_;
  std::vector<double> channel_busy_until_ns_;
  uint32_t write_channel_rr_ = 0;

  uint64_t logical_capacity_pages_ = 0;
  uint64_t free_pages_ = 0;
  // Free blocks are strictly partitioned per channel (never shared), so GC
  // admission decisions must be made on a per-channel free ratio — a global
  // ratio can look healthy while one specific channel is fully exhausted.
  std::vector<uint64_t> free_pages_per_channel_;

  RunMetrics metrics_;

  uint32_t blockOf(uint64_t physical_page) const { return physical_page / geo_.pages_per_block; }
  double freePageRatio() const {
    return static_cast<double>(free_pages_) / geo_.pagesTotal();
  }
  double channelFreeRatio(uint32_t channel) const {
    uint64_t pages_per_channel = geo_.pagesTotal() / geo_.channels;
    return static_cast<double>(free_pages_per_channel_[channel]) / pages_per_channel;
  }

  // Allocates one free physical page on `channel`, advancing the write
  // frontier / pulling a new free block as needed. Never triggers GC itself
  // — callers must ensure a free page exists (checkAndRunGc first).
  uint64_t allocatePage(uint32_t channel);

  void invalidate(uint64_t physical_page);

  // Runs GC on `channel` until free ratio recovers above `target_ratio`,
  // or no reclaimable (non-free, non-active) block remains. Returns pages
  // moved and adds the erase+program cost onto channel_busy_until_ns_.
  uint32_t runGcOnChannel(uint32_t channel, double target_ratio, bool foreground,
                           RunMetrics& m);

  // Foreground/queue-aware/fixed-background admission check, called before
  // servicing a WRITE. May stall (foreground) or steal channel time
  // (background) — both are reflected in channel_busy_until_ns_.
  void checkAndRunGc(uint32_t channel, uint32_t outstanding, RunMetrics& m);
};

}  // namespace ftlsim
