#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

class HalFile {
 public:
  bool open = false;

  uint64_t fileSize64() const { return bytes.size(); }

  bool seek64(const uint64_t offset) {
    if (!open || offset > bytes.size()) return false;
    position = static_cast<size_t>(offset);
    return true;
  }

  int read(void* destination, const size_t length) {
    if (!open) return -1;
    const size_t available = position < bytes.size() ? bytes.size() - position : 0;
    const size_t count = std::min({length, available, readLimit});
    if (count > 0) std::memcpy(destination, bytes.data() + position, count);
    position += count;
    bytesRead += count;
    ++readCalls;
    return static_cast<int>(count);
  }

  size_t bytesRead = 0;
  size_t readCalls = 0;
  size_t position = 0;
  size_t readLimit = std::numeric_limits<size_t>::max();
  std::vector<uint8_t> bytes;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    ++openCount;
    const auto found = files.find(path);
    if (found == files.end()) return false;
    file.bytes = found->second;
    file.position = 0;
    const auto readLimit = readLimits.find(path);
    file.readLimit = readLimit == readLimits.end() ? std::numeric_limits<size_t>::max() : readLimit->second;
    file.open = true;
    return true;
  }

  void put(const std::string& path, std::vector<uint8_t> contents) { files[path] = std::move(contents); }
  void setReadLimit(const std::string& path, const size_t limit) { readLimits[path] = limit; }
  void clear() {
    files.clear();
    readLimits.clear();
    openCount = 0;
  }

  std::unordered_map<std::string, std::vector<uint8_t>> files;
  std::unordered_map<std::string, size_t> readLimits;
  size_t openCount = 0;
};

#define Storage HalStorage::getInstance()
