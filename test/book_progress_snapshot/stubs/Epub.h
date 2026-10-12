#pragma once

#include <string>
#include <utility>

class Epub {
 public:
  Epub(std::string path, std::string cacheDir) : cachePath(std::move(cacheDir) + "/epub_" + std::move(path)) {}
  const std::string& getCachePath() const { return cachePath; }

 private:
  std::string cachePath;
};
