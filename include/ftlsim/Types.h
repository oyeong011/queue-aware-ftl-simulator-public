#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ftlsim {

enum class PageState : uint8_t { FREE, VALID, INVALID };

enum class Op : uint8_t { READ, WRITE };

struct Request {
  uint64_t arrival_ns;
  Op op;
  uint64_t lba;       // logical page number
  uint32_t size_pages; // request size in pages (page-level granularity)
};

enum class GcPolicy { FOREGROUND, FIXED_BACKGROUND, QUEUE_AWARE };

inline GcPolicy parseGcPolicy(const std::string& s) {
  if (s == "foreground") return GcPolicy::FOREGROUND;
  if (s == "fixed_background") return GcPolicy::FIXED_BACKGROUND;
  if (s == "queue_aware") return GcPolicy::QUEUE_AWARE;
  throw std::runtime_error("unknown gc_policy: " + s);
}

// Thrown when a request addresses an LPN outside the provisioned logical
// capacity — callers must handle this as an explicit invalid-LBA failure,
// never as a silent no-op.
struct InvalidLbaError : std::runtime_error {
  explicit InvalidLbaError(const std::string& what) : std::runtime_error(what) {}
};

}  // namespace ftlsim
