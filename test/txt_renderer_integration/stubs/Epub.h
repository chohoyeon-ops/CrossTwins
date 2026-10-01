#pragma once

#include <string>

class Epub {
 public:
  explicit Epub(std::string, const std::string&) {}
  bool load(bool = true, bool = false) { return false; }
  const std::string& getCachePath() const {
    static const std::string empty;
    return empty;
  }
  int getSpineItemsCount() const { return 0; }
  size_t getBookSize() const { return 0; }
  float calculateProgress(int, float) const { return 0; }
};
