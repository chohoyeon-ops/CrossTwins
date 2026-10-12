#pragma once

#include <string>

namespace FsHelpers {
inline bool hasEpubExtension(const std::string& path) {
  return path.size() >= 5 && path.substr(path.size() - 5) == ".epub";
}
inline bool hasXtcExtension(const std::string& path) {
  return path.size() >= 4 && path.substr(path.size() - 4) == ".xtc";
}
}  // namespace FsHelpers
