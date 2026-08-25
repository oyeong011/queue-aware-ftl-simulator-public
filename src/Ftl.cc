#include "ftlsim/Ftl.h"

#include <algorithm>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <vector>

namespace ftlsim {

NandGeometry NandGeometry::fromConfig(const Config& c) {
  NandGeometry g;
  g.page_size_bytes = c.getUInt("page_size_bytes", 4096);
  g.pages_per_block = c.getUInt("pages_per_block", 256);
  g.blocks_per_die = c.getUInt("blocks_per_die", 512);
  g.dies_per_channel = c.getUInt("dies_per_channel", 2);
  g.channels = c.getUInt("channels", 2);
  g.t_read_ns = c.getDouble("t_read_us", 50) * 1000.0;
  g.t_program_ns = c.getDouble("t_program_us", 600) * 1000.0;
  g.t_erase_ns = c.getDouble("t_erase_us", 3000) * 1000.0;
  return g;
}

FtlParams FtlParams::fromConfig(const Config& c) {
  FtlParams p;
  p.over_provisioning_pct = c.getDouble("over_provisioning_pct", 14.0);
  p.critical_free_ratio = c.getDouble("critical_free_ratio", 0.05);
  p.background_free_ratio = c.getDouble("background_free_ratio", 0.20);
  p.low_queue_threshold = c.getUInt("low_queue_threshold", 2);
  p.gc_policy = parseGcPolicy(c.getString("gc_policy", "queue_aware"));
  p.queue_depth = c.getUInt("queue_depth", 16);
  return p;
}

Ftl::Ftl(NandGeometry geo, FtlParams params) : geo_(geo), params_(params) {
  uint64_t total_blocks = geo_.blocksTotal();
  uint64_t total_pages = geo_.pagesTotal();
  uint64_t blocks_per_channel = total_blocks / geo_.channels;

  blocks_.resize(total_blocks);
  for (uint64_t b = 0; b < total_blocks; b++) {
    blocks_[b].channel = static_cast<uint32_t>(b / blocks_per_channel);
  }

  page_state_.assign(total_pages, PageState::FREE);
  p2l_.assign(total_pages, -1);

  logical_capacity_pages_ = static_cast<uint64_t>(
      total_pages * (1.0 - params_.over_provisioning_pct / 100.0));
  l2p_.assign(logical_capacity_pages_, -1);

  free_blocks_per_channel_.resize(geo_.channels);
  for (uint64_t b = 0; b < total_blocks; b++) {
    free_blocks_per_channel_[blocks_[b].channel].push_back(static_cast<uint32_t>(b));
  }

  active_block_per_channel_.assign(geo_.channels, 0);
  active_offset_per_channel_.assign(geo_.channels, geo_.pages_per_block);  // force pull on first use
  channel_busy_until_ns_.assign(geo_.channels, 0.0);

  free_pages_ = total_pages;
  free_pages_per_channel_.assign(geo_.channels, total_pages / geo_.channels);
}

uint64_t Ftl::allocatePage(uint32_t channel) {
  if (active_offset_per_channel_[channel] >= geo_.pages_per_block) {
    if (free_blocks_per_channel_[channel].empty()) {
      throw std::runtime_error("NAND full: no free block on channel " + std::to_string(channel));
    }
    uint32_t nb = free_blocks_per_channel_[channel].front();
    free_blocks_per_channel_[channel].pop_front();
    blocks_[nb].is_free = false;
    active_block_per_channel_[channel] = nb;
    active_offset_per_channel_[channel] = 0;
  }
  uint32_t blk = active_block_per_channel_[channel];
  uint32_t off = active_offset_per_channel_[channel]++;
  return static_cast<uint64_t>(blk) * geo_.pages_per_block + off;
}

void Ftl::invalidate(uint64_t physical_page) {
  page_state_[physical_page] = PageState::INVALID;
  Block& b = blocks_[blockOf(physical_page)];
  b.valid_count--;
  b.invalid_count++;
}

uint32_t Ftl::runGcOnChannel(uint32_t channel, double /*target_ratio*/, bool foreground,
                              RunMetrics& m) {
  int64_t victim = -1;
  uint16_t best = UINT16_MAX;
  uint64_t blocks_per_channel = geo_.blocksTotal() / geo_.channels;
  uint64_t base = static_cast<uint64_t>(channel) * blocks_per_channel;
  for (uint64_t b = base; b < base + blocks_per_channel; b++) {
    const Block& blk = blocks_[b];
    if (blk.is_free) continue;
    if (b == active_block_per_channel_[channel]) continue;
    if (blk.invalid_count == 0) continue;
    if (blk.valid_count < best) {
      best = blk.valid_count;
      victim = static_cast<int64_t>(b);
    }
  }
  if (victim < 0) return 0;

  uint32_t moved = 0;
  uint64_t vbase = static_cast<uint64_t>(victim) * geo_.pages_per_block;
  for (uint32_t off = 0; off < geo_.pages_per_block; off++) {
    uint64_t p = vbase + off;
    if (page_state_[p] != PageState::VALID) continue;
    int64_t lpn = p2l_[p];
    uint64_t newp = allocatePage(channel);
    l2p_[lpn] = static_cast<int64_t>(newp);
    p2l_[newp] = lpn;
    page_state_[newp] = PageState::VALID;
    blocks_[blockOf(newp)].valid_count++;
    free_pages_--;
    free_pages_per_channel_[channel]--;

    page_state_[p] = PageState::INVALID;
    p2l_[p] = -1;
    moved++;
    m.nand_write_pages++;
  }

  Block& vb = blocks_[victim];
  vb.valid_count = 0;
  vb.invalid_count = 0;
  vb.erase_count++;
  vb.is_free = true;
  for (uint32_t off = 0; off < geo_.pages_per_block; off++) {
    page_state_[vbase + off] = PageState::FREE;
  }
  free_pages_ += geo_.pages_per_block;
  free_pages_per_channel_[channel] += geo_.pages_per_block;
  free_blocks_per_channel_[channel].push_back(static_cast<uint32_t>(victim));

  double cost = geo_.t_erase_ns + moved * (geo_.t_read_ns + geo_.t_program_ns);
  channel_busy_until_ns_[channel] += cost;

  m.gc_count++;
  if (foreground) {
    m.foreground_gc_count++;
    m.gc_induced_stall_ns += static_cast<uint64_t>(cost);
  } else {
    m.background_gc_count++;
  }
  m.pages_moved_by_gc += moved;
  return moved;
}

void Ftl::checkAndRunGc(uint32_t channel, uint32_t outstanding, RunMetrics& m) {
  // Critical: force GC (foreground) until at least one free page is available
  // and this channel specifically is above the critical threshold. Must be
  // per-channel — free blocks never move between channels, so a global ratio
  // can look healthy while this channel alone is exhausted. Bounded — a
  // missing victim means genuinely full NAND, which fails loudly instead of
  // spinning.
  while (free_pages_per_channel_[channel] == 0 ||
         channelFreeRatio(channel) <= params_.critical_free_ratio) {
    uint32_t moved = runGcOnChannel(channel, params_.critical_free_ratio, /*foreground=*/true, m);
    if (moved == 0) {
      if (free_pages_per_channel_[channel] == 0) {
        throw std::runtime_error("NAND full: no reclaimable block on channel " +
                                  std::to_string(channel));
      }
      break;
    }
  }

  double ratio = channelFreeRatio(channel);
  if (ratio <= params_.background_free_ratio) {
    bool trigger = (params_.gc_policy == GcPolicy::FIXED_BACKGROUND) ||
                   (params_.gc_policy == GcPolicy::QUEUE_AWARE &&
                    outstanding <= params_.low_queue_threshold);
    if (trigger) {
      runGcOnChannel(channel, params_.background_free_ratio, /*foreground=*/false, m);
    }
  }
}

RunMetrics Ftl::run(const std::vector<Request>& trace) {
  metrics_ = RunMetrics{};
  metrics_.request_count = trace.size();

  std::priority_queue<double, std::vector<double>, std::greater<double>> inflight;
  double queue_wait_sum = 0.0;
  double device_time_sum = 0.0;

  for (const auto& req : trace) {
    if (req.lba + req.size_pages > logical_capacity_pages_) {
      throw InvalidLbaError("LBA out of range: lba=" + std::to_string(req.lba) +
                             " size=" + std::to_string(req.size_pages) +
                             " capacity=" + std::to_string(logical_capacity_pages_));
    }

    double start = static_cast<double>(req.arrival_ns);
    // Drop already-completed in-flight entries first — otherwise `inflight`
    // just accumulates the queue_depth most recent completions ever pushed
    // and never actually shrinks, so "outstanding" would stay pinned at
    // queue_depth-1 forever even across long idle gaps.
    while (!inflight.empty() && inflight.top() <= start) inflight.pop();
    if (inflight.size() >= params_.queue_depth) {
      double earliest = inflight.top();
      inflight.pop();
      start = std::max(start, earliest);
    }
    uint32_t outstanding = static_cast<uint32_t>(inflight.size());

    uint32_t channel;
    if (req.op == Op::WRITE) {
      channel = write_channel_rr_ % geo_.channels;
      write_channel_rr_++;
      checkAndRunGc(channel, outstanding, metrics_);
    } else {
      int64_t p = (req.lba < l2p_.size()) ? l2p_[req.lba] : -1;
      channel = (p >= 0) ? blocks_[blockOf(static_cast<uint64_t>(p))].channel : 0;
    }

    double service_start = std::max(start, channel_busy_until_ns_[channel]);
    double queue_wait = service_start - static_cast<double>(req.arrival_ns);

    double device_time;
    if (req.op == Op::READ) {
      device_time = geo_.t_read_ns * req.size_pages;
    } else {
      device_time = geo_.t_program_ns * req.size_pages;
      for (uint32_t i = 0; i < req.size_pages; i++) {
        uint64_t lpn = req.lba + i;
        int64_t old_p = l2p_[lpn];
        if (old_p >= 0) invalidate(static_cast<uint64_t>(old_p));
        uint64_t newp = allocatePage(channel);
        l2p_[lpn] = static_cast<int64_t>(newp);
        p2l_[newp] = static_cast<int64_t>(lpn);
        page_state_[newp] = PageState::VALID;
        blocks_[blockOf(newp)].valid_count++;
        free_pages_--;
        free_pages_per_channel_[channel]--;
        metrics_.host_write_pages++;
        metrics_.nand_write_pages++;
      }
    }

    double completion = service_start + device_time;
    channel_busy_until_ns_[channel] = completion;
    inflight.push(completion);

    double latency = completion - static_cast<double>(req.arrival_ns);
    metrics_.latencies_ns.push_back(latency);
    queue_wait_sum += queue_wait;
    device_time_sum += device_time;
    metrics_.completed_request_count++;
    metrics_.simulated_time_ns =
        std::max(metrics_.simulated_time_ns, static_cast<uint64_t>(completion));
  }

  metrics_.finalizeLatencyStats();
  metrics_.avg_queue_wait_ns =
      metrics_.completed_request_count ? queue_wait_sum / metrics_.completed_request_count : 0.0;
  metrics_.avg_device_service_ns =
      metrics_.completed_request_count ? device_time_sum / metrics_.completed_request_count : 0.0;

  metrics_.waf = metrics_.host_write_pages
                     ? static_cast<double>(metrics_.nand_write_pages) / metrics_.host_write_pages
                     : 1.0;

  metrics_.free_page_ratio_end = freePageRatio();
  uint64_t invalid_total = 0;
  for (const auto& b : blocks_) invalid_total += b.invalid_count;
  metrics_.invalid_page_ratio_end = static_cast<double>(invalid_total) / geo_.pagesTotal();

  std::vector<uint32_t> erase_counts;
  erase_counts.reserve(blocks_.size());
  for (const auto& b : blocks_) erase_counts.push_back(b.erase_count);
  RunMetrics::eraseStats(erase_counts, metrics_.erase_count_mean, metrics_.erase_count_stddev,
                          metrics_.erase_count_max);

  if (metrics_.simulated_time_ns > 0) {
    metrics_.throughput_reqs_per_sec =
        metrics_.completed_request_count / (metrics_.simulated_time_ns / 1e9);
  }

  return metrics_;
}

bool Ftl::validateInvariants(std::string* err) const {
  auto fail = [&](const std::string& msg) {
    if (err) *err = msg;
    return false;
  };

  uint64_t free_count = 0, valid_count = 0, invalid_count = 0;
  for (auto s : page_state_) {
    if (s == PageState::FREE) free_count++;
    else if (s == PageState::VALID) valid_count++;
    else invalid_count++;
  }
  if (free_count != free_pages_) return fail("free page count mismatch");
  uint64_t per_channel_sum = 0;
  for (auto v : free_pages_per_channel_) per_channel_sum += v;
  if (per_channel_sum != free_pages_) return fail("per-channel free page sum mismatch");
  if (free_count + valid_count + invalid_count != geo_.pagesTotal()) {
    return fail("free+valid+invalid != total pages");
  }

  uint64_t block_valid_sum = 0, block_invalid_sum = 0;
  for (const auto& b : blocks_) {
    block_valid_sum += b.valid_count;
    block_invalid_sum += b.invalid_count;
  }
  if (block_valid_sum != valid_count) return fail("block valid_count sum mismatch");
  if (block_invalid_sum != invalid_count) return fail("block invalid_count sum mismatch");

  uint64_t mapped = 0;
  for (uint64_t lpn = 0; lpn < l2p_.size(); lpn++) {
    int64_t p = l2p_[lpn];
    if (p < 0) continue;
    mapped++;
    if (page_state_[p] != PageState::VALID) return fail("mapped LPN points to non-VALID page");
    if (p2l_[p] != static_cast<int64_t>(lpn)) return fail("p2l/l2p mismatch");
  }
  if (mapped != valid_count) return fail("mapped LPN count != VALID page count (dangling valid page)");

  return true;
}

}  // namespace ftlsim
