#include "BookProgress.h"

#include <Epub.h>
#include <Epub/BookMetadataCache.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include <cstdint>

namespace {
uint32_t readLe32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}  // namespace

BookProgressSnapshot loadBookProgressSnapshot(const std::string& path, const bool includeChapter) {
  BookProgressSnapshot snapshot;
  uint8_t data[10]{};
  if (FsHelpers::hasEpubExtension(path)) {
    // Only derive the cache path here; loading Epub would allocate cumulative sizes for every spine.
    auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("HOME", "OOM: progress metadata");
      return snapshot;
    }
    HalFile file;
    if (!Storage.openFileForRead("HOME", epub->getCachePath() + "/progress.bin", file)) return snapshot;
    const uint64_t fileSize = file.fileSize64();
    if (fileSize != 4 && fileSize != 6 && fileSize != 10) return snapshot;
    const int size = file.read(data, sizeof(data));
    if (size != static_cast<int>(fileSize)) return snapshot;
    const int spine = data[0] | (data[1] << 8);
    const int page = data[2] | (data[3] << 8);
    const int total = size >= 6 ? data[4] | (data[5] << 8) : 0;
    BookMetadataCache::loadProgressSnapshot(epub->getCachePath(), static_cast<uint16_t>(spine),
                                            static_cast<uint16_t>(page), static_cast<uint16_t>(total), includeChapter,
                                            snapshot.percent, snapshot.chapterTitle, sizeof(snapshot.chapterTitle));
    return snapshot;
  }
  if (FsHelpers::hasXtcExtension(path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("HOME", "OOM: XTC progress metadata");
      return snapshot;
    }
    if (!xtc->load()) return snapshot;
    HalFile file;
    if (!Storage.openFileForRead("HOME", xtc->getCachePath() + "/progress.bin", file) || file.read(data, 4) != 4)
      return snapshot;
    const uint32_t page = readLe32(data);
    if (xtc->getPageCount() == 0) return snapshot;
    if (page >= xtc->getPageCount()) {
      snapshot.percent = 100;
      return snapshot;
    }
    snapshot.percent = xtc->calculateProgress(page);
  }
  return snapshot;
}

int loadBookProgress(const std::string& path) { return loadBookProgressSnapshot(path).percent; }
