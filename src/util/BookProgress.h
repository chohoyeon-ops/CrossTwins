#pragma once

#include <cstddef>
#include <string>

struct BookProgressSnapshot {
  static constexpr size_t CHAPTER_TITLE_CAPACITY = 257;

  int percent = -1;
  char chapterTitle[CHAPTER_TITLE_CAPACITY]{};
};

// Saved progress and, for EPUBs when requested, a bounded UTF-8 chapter title.
// Does not build reading caches or load the EPUB spine into memory.
BookProgressSnapshot loadBookProgressSnapshot(const std::string& path, bool includeChapter = false);

// Saved reading percentage, or -1 when unavailable. Never builds reading caches.
int loadBookProgress(const std::string& path);
