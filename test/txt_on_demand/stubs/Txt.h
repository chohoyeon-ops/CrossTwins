#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

class Txt {
 public:
  explicit Txt(std::string content, std::string path = "/books/book.txt",
                std::string cachePath = "/.crosspoint/txt_test")
      : content(std::move(content)), path(std::move(path)), cachePath(std::move(cachePath)) {}

  size_t getFileSize() const { return content.size(); }
  const std::string& getPath() const { return path; }
  const std::string& getCachePath() const { return cachePath; }

  bool readContent(uint8_t* buffer, size_t offset, size_t length) const {
    reads.emplace_back(offset, length);
    if (failNextRead) {
      failNextRead = false;
      return false;
    }
    if (failReads || offset > content.size() || length > content.size() - offset) return false;
    const size_t bytesToCopy = shortRead && length > 0 ? length - 1 : length;
    std::memcpy(buffer, content.data() + offset, bytesToCopy);
    if (shortRead) return false;
    return true;
  }

  std::string content;
  std::string path;
  std::string cachePath;
  mutable std::vector<std::pair<size_t, size_t>> reads;
  mutable bool failNextRead = false;
  mutable bool failReads = false;
  mutable bool shortRead = false;
};
