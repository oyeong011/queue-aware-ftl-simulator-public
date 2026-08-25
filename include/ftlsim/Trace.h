#pragma once
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ftlsim/Types.h"

namespace ftlsim {

// CSV schema: arrival_ns,op,lba,size_bytes,stream_id (header row required).
// lba/size_bytes are in page units of `page_size_bytes` — size_bytes must be
// a multiple of it. stream_id is parsed but not yet consumed by the FTL.
inline std::vector<Request> loadTrace(const std::string& path, uint32_t page_size_bytes) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open trace: " + path);
  std::string line;
  std::getline(f, line);  // header

  std::vector<Request> trace;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    std::stringstream ss(line);
    std::string arrival_s, op_s, lba_s, size_s, stream_s;
    std::getline(ss, arrival_s, ',');
    std::getline(ss, op_s, ',');
    std::getline(ss, lba_s, ',');
    std::getline(ss, size_s, ',');
    std::getline(ss, stream_s, ',');

    Request r;
    r.arrival_ns = std::stoull(arrival_s);
    r.op = (op_s == "WRITE") ? Op::WRITE : Op::READ;
    r.lba = std::stoull(lba_s);
    uint64_t size_bytes = std::stoull(size_s);
    if (size_bytes % page_size_bytes != 0) {
      throw std::runtime_error("size_bytes not a multiple of page_size_bytes: " + line);
    }
    r.size_pages = static_cast<uint32_t>(size_bytes / page_size_bytes);
    trace.push_back(r);
  }
  return trace;
}

}  // namespace ftlsim
