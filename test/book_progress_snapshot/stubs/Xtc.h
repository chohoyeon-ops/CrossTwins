#pragma once

#include <cstdint>
#include <string>
#include <utility>

class Xtc {
 public:
  Xtc(std::string, std::string cacheDir) : cachePath(std::move(cacheDir) + "/xtc_test") {}
  bool load() { return true; }
  const std::string& getCachePath() const { return cachePath; }
  uint32_t getPageCount() const { return 100; }
  uint8_t calculateProgress(uint32_t page) const { return static_cast<uint8_t>(page); }

 private:
  std::string cachePath;
};
