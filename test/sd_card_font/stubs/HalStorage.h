#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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
  struct State {
    size_t position = 0;
    bool opened = false;
    ~State() { opened = false; }
  };
  std::unique_ptr<State> state;

 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  bool seekSet(size_t position) {
    state->position = position;
    return state->opened && position <= sdFontTestFile.size();
  }
  int read(void* output, size_t count) {
    if (sdFontTestReadFailure) return 0;
    if (!state->opened || state->position > sdFontTestFile.size()) return 0;
    count = std::min(count, sdFontTestFile.size() - state->position);
    std::memcpy(output, sdFontTestFile.data() + state->position, count);
    state->position += count;
    sdFontTestReads++;
    return static_cast<int>(count);
  }
  bool isOpen() const { return state && state->opened; }
  explicit operator bool() const { return isOpen(); }
  void close() {
    if (!state) {
      std::fputs("HalFile::close on empty owner\n", stderr);
      std::abort();
    }
    state->opened = false;
  }
  void allocateOwner() { state = std::make_unique<State>(); }
  void open() {
    if (!state) allocateOwner();
    state->opened = true;
    state->position = 0;
  }
};

class HalStorage {
 public:
  enum class ReadOpenResult : uint8_t { Ok, IoFailure, ResourceFailure };
  static constexpr size_t READ_HANDLE_ALLOCATION_LIMIT = 256;
  ReadOpenResult openFileForReadChecked(const char*, const char*, HalFile& file) {
    if (sdFontTestHandleFailure) return ReadOpenResult::ResourceFailure;
    HalFile candidate;
    candidate.allocateOwner();
    if (sdFontTestOpenFailure) return ReadOpenResult::IoFailure;
    candidate.open();
    file = std::move(candidate);
    return ReadOpenResult::Ok;
  }

  bool openFileForRead(const char*, const char*, HalFile& file) {
    file.open();
    return true;
  }
};
inline HalStorage Storage;
