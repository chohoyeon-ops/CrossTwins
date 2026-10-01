#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include <memory>

inline std::vector<uint8_t> sdFontTestFile;
inline std::map<std::string, std::vector<uint8_t>> readerTestFiles;
inline std::string readerTestShortReadPath;
inline size_t readerTestShortReadAfter = 0;
inline std::map<std::string,size_t> readerTestPathReads;
inline std::string readerTestShortWritePath;
inline std::string readerTestRenameFailureDestination;
inline bool readerTestRenameFailure = false;
inline size_t readerTestProgressWrites = 0;
inline size_t sdFontTestReads = 0;
inline bool sdFontTestReadFailure = false;
inline bool sdFontTestOpenFailure = false;
inline bool sdFontTestHandleFailure = false;


struct SdFontTestEsp {
  void restart() {}
  size_t largestBlock = 200 * 1024;
  size_t getFreeHeap() const { return 200 * 1024; }
  size_t getMaxAllocHeap() const { return largestBlock; }
};
inline SdFontTestEsp ESP;
inline uint32_t millis() { return 0; }

class HalFile {
  struct State { size_t position_=0; bool opened_=false; std::vector<uint8_t>* bytes_=nullptr; std::string path_; };
  std::unique_ptr<State> state = std::make_unique<State>();
 public:
  bool seekSet(size_t position) { state->position_ = position; return state->opened_ && position <= state->bytes_->size(); }
  bool seek(size_t position) { return seekSet(position); }
  size_t size() const { return state->opened_ ? state->bytes_->size() : 0; }
  bool available() const { return state->opened_ && state->position_ < state->bytes_->size(); }
  explicit operator bool() const { return state->opened_; }
  int read(void* output, size_t count) {
    if (sdFontTestReadFailure || !state->opened_ || state->position_ > state->bytes_->size()) return 0;
    count = std::min(count, state->bytes_->size() - state->position_);
    if (state->path_ == readerTestShortReadPath && readerTestPathReads[state->path_]++ >= readerTestShortReadAfter && count != 0) --count;
    std::memcpy(output, state->bytes_->data() + state->position_, count);
    state->position_ += count;
    sdFontTestReads++;
    return static_cast<int>(count);
  }
  size_t write(const void* data, size_t count) {
    if (!state->opened_) return 0;
    if (state->path_ == readerTestShortWritePath && count != 0) --count;
    state->bytes_->resize(std::max(state->bytes_->size(), state->position_ + count));
    std::memcpy(state->bytes_->data() + state->position_, data, count);
    state->position_ += count;
    if (state->path_.find("progress.bin") != std::string::npos) ++readerTestProgressWrites;
    return count;
  }
  void flush() {}
  void close() { state->opened_ = false; }
  void open(const std::string& path = "") {
    state->path_ = path;
    const auto it = readerTestFiles.find(path);
    state->bytes_ = it == readerTestFiles.end() ? &sdFontTestFile : &it->second;
    state->opened_ = true; state->position_ = 0;
  }
};

class HalStorage {
 public:
  enum class ReadOpenResult : uint8_t { Ok, IoFailure, ResourceFailure };
  static constexpr size_t READ_HANDLE_ALLOCATION_LIMIT = 256;
  ReadOpenResult openFileForReadChecked(const char*, const char* path, HalFile& file) {
    if (sdFontTestHandleFailure) return ReadOpenResult::ResourceFailure;
    if (sdFontTestOpenFailure) return ReadOpenResult::IoFailure;
    if (readerTestFiles.count(path) == 0 && std::string(path).find(".cpfont") == std::string::npos) return ReadOpenResult::IoFailure;
    file.open(path); return ReadOpenResult::Ok;
  }
  ReadOpenResult openFileForWriteChecked(const char*, const char* path, HalFile& file) {
    if (sdFontTestHandleFailure) return ReadOpenResult::ResourceFailure;
    if (sdFontTestOpenFailure) return ReadOpenResult::IoFailure;
    readerTestFiles[path].clear(); file.open(path); return ReadOpenResult::Ok;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) { file.open(path); return true; }
  bool openFileForRead(const char* module, const std::string& path, HalFile& file) {
    if (readerTestFiles.count(path) == 0 && path.find(".cpfont") == std::string::npos) return false;
    return openFileForRead(module, path.c_str(), file);
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    readerTestFiles[path].clear(); file.open(path); return true;
  }
  bool exists(const char* path) { return readerTestFiles.count(path) != 0; }
  bool mkdir(const char*) { return true; }
  bool removeDir(const char*) { return true; }
  bool remove(const char* path) { return readerTestFiles.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (readerTestRenameFailure || to == readerTestRenameFailureDestination || !exists(from) || exists(to)) return false;
    readerTestFiles[to] = std::move(readerTestFiles[from]); readerTestFiles.erase(from); return true;
  }
};
inline HalStorage Storage;
