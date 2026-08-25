#pragma once
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <stdexcept>

namespace ftlsim {

// ponytail: flat key=value config instead of pulling in a JSON dependency —
// upgrade to nlohmann/json only if configs grow nested.
class Config {
 public:
  static Config load(const std::string& path) {
    Config c;
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open config: " + path);
    std::string line;
    while (std::getline(f, line)) {
      auto hash = line.find('#');
      if (hash != std::string::npos) line = line.substr(0, hash);
      auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string key = trim(line.substr(0, eq));
      std::string val = trim(line.substr(eq + 1));
      if (!key.empty()) c.values_[key] = val;
    }
    return c;
  }

  double getDouble(const std::string& key, double def) const {
    auto it = values_.find(key);
    return it == values_.end() ? def : std::stod(it->second);
  }
  uint64_t getUInt(const std::string& key, uint64_t def) const {
    auto it = values_.find(key);
    return it == values_.end() ? def : std::stoull(it->second);
  }
  std::string getString(const std::string& key, const std::string& def) const {
    auto it = values_.find(key);
    return it == values_.end() ? def : it->second;
  }

 private:
  static std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
  }
  std::unordered_map<std::string, std::string> values_;
};

}  // namespace ftlsim
