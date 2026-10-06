#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

class HalStorage;
class HalFile {
  HalStorage* storage = nullptr;
  std::string path;
  size_t position = 0;
  bool writable = false;
  bool open = false;
 public:
  HalFile() = default;
  HalFile(HalStorage* owner, std::string filePath, bool canWrite)
      : storage(owner), path(std::move(filePath)), writable(canWrite), open(true) {}
  size_t size() const;
  int read(void* buffer, size_t count);
  size_t write(const uint8_t* buffer, size_t count);
  void flush() {}
  bool close() { open = false; return true; }
};

class HalStorage {
 public:
  std::map<std::string, std::vector<uint8_t>> files;
  bool failReadOpen = false;
  bool failWriteOpen = false;
  bool failRemove = false;
  bool failRename = false;
  std::string failNextReadPath;
  mutable std::string failNextExistsPath;
  size_t writeLimit = std::numeric_limits<size_t>::max();

  static HalStorage& getInstance() { static HalStorage storage; return storage; }
  void clear() {
    files.clear();
    failReadOpen = false;
    failWriteOpen = false;
    failRemove = false;
    failRename = false;
    failNextReadPath.clear();
    failNextExistsPath.clear();
    writeLimit = std::numeric_limits<size_t>::max();
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    if (path == failNextReadPath) {
      failNextReadPath.clear();
      return false;
    }
    if (failReadOpen || files.find(path) == files.end()) return false;
    file = HalFile(this, path, false);
    return true;
  }
  bool openFileForRead(const char* module, const char* path, HalFile& file) {
    return openFileForRead(module, std::string(path), file);
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    if (failWriteOpen) return false;
    files[path].clear();
    file = HalFile(this, path, true);
    return true;
  }
  bool openFileForWrite(const char* module, const char* path, HalFile& file) {
    return openFileForWrite(module, std::string(path), file);
  }
  bool exists(const char* path) const {
    if (failNextExistsPath == path) {
      failNextExistsPath.clear();
      return false;
    }
    return files.find(path) != files.end();
  }
  bool remove(const char* path) {
    if (failRemove) return false;
    return files.erase(path) != 0;
  }
  bool rename(const char* oldPath, const char* newPath) {
    if (failRename || files.find(oldPath) == files.end() || files.find(newPath) != files.end()) return false;
    files.emplace(newPath, std::move(files.at(oldPath)));
    files.erase(oldPath);
    return true;
  }
};

inline size_t HalFile::size() const {
  if (!open || storage == nullptr) return 0;
  const auto it = storage->files.find(path);
  return it == storage->files.end() ? 0 : it->second.size();
}
inline int HalFile::read(void* buffer, const size_t count) {
  if (!open || storage == nullptr) return -1;
  const auto it = storage->files.find(path);
  if (it == storage->files.end() || position > it->second.size()) return -1;
  const size_t amount = std::min(count, it->second.size() - position);
  std::memcpy(buffer, it->second.data() + position, amount);
  position += amount;
  return static_cast<int>(amount);
}
inline size_t HalFile::write(const uint8_t* buffer, const size_t count) {
  if (!open || !writable || storage == nullptr) return 0;
  auto& data = storage->files[path];
  const size_t amount = std::min(count, storage->writeLimit);
  if (position + amount > data.size()) data.resize(position + amount);
  std::memcpy(data.data() + position, buffer, amount);
  position += amount;
  return amount;
}
#define Storage HalStorage::getInstance()
