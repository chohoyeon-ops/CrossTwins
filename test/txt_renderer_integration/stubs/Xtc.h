#pragma once

#include <cstdint>
#include <string>

class Xtc {
 public:
  explicit Xtc(std::string, const std::string&) {}
  bool load() { return false; }
  std::string getCachePath() const { return {}; }
  uint32_t getPageCount() const { return 0; }
  uint8_t calculateProgress(uint32_t) const { return 0; }
};
