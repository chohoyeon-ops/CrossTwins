#pragma once

#include <Epub/Epub/ProgressSnapshotReader.h>
#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>

class BookMetadataCache {
 public:
  static constexpr uint8_t BOOK_CACHE_VERSION = 10;

  static bool loadProgressSnapshot(const std::string& cachePath, const uint16_t spineIndex, const uint16_t page,
                                   const uint16_t totalPages, const bool includeChapter, int& percent,
                                   char* chapterTitle, const size_t chapterTitleCapacity) {
    HalFile file;
    if (!Storage.openFileForRead("BMC", cachePath + "/book.bin", file)) return false;
    const bool loaded = epub::readBookBinProgressSnapshot(file, BOOK_CACHE_VERSION, spineIndex, page, totalPages,
                                                          includeChapter, percent, chapterTitle, chapterTitleCapacity);
    lastBookBinBytesRead = file.bytesRead;
    lastBookBinReadCalls = file.readCalls;
    return loaded;
  }

  static size_t lastBookBinBytesRead;
  static size_t lastBookBinReadCalls;
};

inline size_t BookMetadataCache::lastBookBinBytesRead = 0;
inline size_t BookMetadataCache::lastBookBinReadCalls = 0;
