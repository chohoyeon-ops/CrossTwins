#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

inline std::vector<uint8_t> sdFontTestFile;
inline size_t sdFontTestReads = 0;
inline bool sdFontTestReadFailure = false;
inline bool sdFontTestOpenFailure = false;
inline bool sdFontTestHandleFailure = false;


struct SdFontTestEsp {
  size_t largestBlock = 200 * 1024;
  size_t getFreeHeap() const { return 200 * 1024; }
  size_t getMaxAllocHeap() const { return largestBlock; }
};
inline SdFontTestEsp ESP;
inline uint32_t millis() { return 0; }

class HalFile {
 public:
  bool seekSet(size_t position) {
    position_ = position;
    return opened_ && position <= sdFontTestFile.size();
  }
  int read(void* output, size_t count) {
    if (sdFontTestReadFailure) return 0;
    if (!opened_ || position_ > sdFontTestFile.size()) return 0;
    count = std::min(count, sdFontTestFile.size() - position_);
    std::memcpy(output, sdFontTestFile.data() + position_, count);
    position_ += count;
    sdFontTestReads++;
    return static_cast<int>(count);
  }
  void close() { opened_ = false; }
  void open() {
    opened_ = true;
    position_ = 0;
  }

 private:
  size_t position_ = 0;
  bool opened_ = false;
};

class HalStorage {
 public:
  enum class ReadOpenResult : uint8_t { Ok, IoFailure, ResourceFailure };
  static constexpr size_t READ_HANDLE_ALLOCATION_LIMIT = 256;
  ReadOpenResult openFileForReadChecked(const char*, const char*, HalFile& file) {
    if (sdFontTestHandleFailure) return ReadOpenResult::ResourceFailure;
    if (sdFontTestOpenFailure) return ReadOpenResult::IoFailure;
    file.open();
    return ReadOpenResult::Ok;
  }

  bool openFileForRead(const char*, const char*, HalFile& file) {
    file.open();
    return true;
  }
};
inline HalStorage Storage;
