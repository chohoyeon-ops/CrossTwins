#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "EpubProgress.h"

namespace epub {
namespace detail {

constexpr uint32_t MAX_BOOK_BIN_STRING_BYTES = 65535;
constexpr size_t MAX_PROGRESS_CHAPTER_TITLE_BYTES = 256;

inline uint16_t readLe16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

inline uint32_t readLe32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

inline int16_t readSignedLe16(const uint8_t* bytes) {
  const uint16_t raw = readLe16(bytes);
  const int32_t value = (raw & 0x8000) != 0 ? static_cast<int32_t>(raw) - 0x10000 : raw;
  return static_cast<int16_t>(value);
}

template <typename Reader>
bool readAt(Reader& reader, const uint64_t fileSize, const uint64_t offset, void* destination, const size_t length) {
  if (offset > fileSize || length > fileSize - offset ||
      length > static_cast<size_t>(std::numeric_limits<int>::max()) || !reader.seek64(offset)) {
    return false;
  }
  return reader.read(destination, length) == static_cast<int>(length);
}

template <typename Reader>
bool readLength(Reader& reader, const uint64_t fileSize, const uint64_t offset, uint32_t& length) {
  uint8_t bytes[4];
  if (!readAt(reader, fileSize, offset, bytes, sizeof(bytes))) return false;
  length = readLe32(bytes);
  return length <= MAX_BOOK_BIN_STRING_BYTES && length <= fileSize - offset - sizeof(bytes);
}

inline bool isValidUtf8(const uint8_t* text, const size_t length) {
  size_t i = 0;
  while (i < length) {
    const uint8_t lead = text[i++];
    if (lead <= 0x7F) continue;

    uint8_t continuationCount = 0;
    uint8_t secondMin = 0x80;
    uint8_t secondMax = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
      continuationCount = 1;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      continuationCount = 2;
      if (lead == 0xE0) secondMin = 0xA0;
      if (lead == 0xED) secondMax = 0x9F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      continuationCount = 3;
      if (lead == 0xF0) secondMin = 0x90;
      if (lead == 0xF4) secondMax = 0x8F;
    } else {
      return false;
    }

    if (continuationCount > length - i) return false;
    const uint8_t second = text[i++];
    if (second < secondMin || second > secondMax) return false;
    for (uint8_t j = 1; j < continuationCount; ++j) {
      const uint8_t continuation = text[i++];
      if (continuation < 0x80 || continuation > 0xBF) return false;
    }
  }
  return true;
}

struct SpineSample {
  uint64_t position = 0;
  uint64_t end = 0;
  uint32_t cumulativeSize = 0;
  int16_t tocIndex = -1;
};

struct TocSample {
  uint64_t position = 0;
  uint64_t titlePosition = 0;
  uint32_t titleLength = 0;
  int16_t spineIndex = -1;
};

template <typename Reader>
bool readSpineEntryPosition(Reader& reader, const uint64_t fileSize, const uint32_t lutOffset,
                            const uint16_t spineCount, const uint64_t spineEntriesStart, const uint16_t index,
                            uint64_t& entryPosition) {
  uint8_t positionBytes[4];
  const uint64_t lutPosition = static_cast<uint64_t>(lutOffset) + static_cast<uint64_t>(index) * sizeof(uint32_t);
  if (index >= spineCount || !readAt(reader, fileSize, lutPosition, positionBytes, sizeof(positionBytes))) {
    return false;
  }

  entryPosition = readLe32(positionBytes);
  if (entryPosition < spineEntriesStart || entryPosition > fileSize || fileSize - entryPosition < sizeof(uint32_t)) {
    return false;
  }
  return true;
}

template <typename Reader>
bool readTocEntryPosition(Reader& reader, const uint64_t fileSize, const uint32_t lutOffset, const uint16_t spineCount,
                          const uint16_t tocCount, const uint64_t tocEntriesStart, const uint16_t index,
                          uint64_t& entryPosition) {
  if (index >= tocCount) return false;
  const uint64_t tocLutOffset = static_cast<uint64_t>(lutOffset) +
                                static_cast<uint64_t>(spineCount) * sizeof(uint32_t) +
                                static_cast<uint64_t>(index) * sizeof(uint32_t);
  uint8_t positionBytes[4];
  if (!readAt(reader, fileSize, tocLutOffset, positionBytes, sizeof(positionBytes))) return false;

  entryPosition = readLe32(positionBytes);
  return entryPosition >= tocEntriesStart && entryPosition <= fileSize;
}

template <typename Reader>
bool readTocSample(Reader& reader, const uint64_t fileSize, const uint64_t tocEntriesStart, const uint16_t spineCount,
                   const uint64_t entryPosition, const uint64_t entryLimit, TocSample& sample) {
  if (entryLimit > fileSize || entryPosition < tocEntriesStart || entryPosition >= entryLimit) return false;

  uint32_t titleLength = 0;
  if (!readLength(reader, entryLimit, entryPosition, titleLength)) return false;
  const uint64_t titlePosition = entryPosition + sizeof(uint32_t);
  const uint64_t hrefLengthPosition = titlePosition + titleLength;
  uint32_t hrefLength = 0;
  if (!readLength(reader, entryLimit, hrefLengthPosition, hrefLength)) return false;
  const uint64_t anchorLengthPosition = hrefLengthPosition + sizeof(uint32_t) + hrefLength;
  uint32_t anchorLength = 0;
  if (!readLength(reader, entryLimit, anchorLengthPosition, anchorLength)) return false;
  const uint64_t fieldsPosition = anchorLengthPosition + sizeof(uint32_t) + anchorLength;
  uint8_t fields[sizeof(uint8_t) + sizeof(int16_t)];
  if (!readAt(reader, entryLimit, fieldsPosition, fields, sizeof(fields)) ||
      fieldsPosition + sizeof(fields) > entryLimit) {
    return false;
  }

  const int16_t spineIndex = readSignedLe16(fields + sizeof(uint8_t));
  if (spineIndex < -1 || spineIndex >= spineCount) return false;

  sample.position = entryPosition;
  sample.titlePosition = titlePosition;
  sample.titleLength = titleLength;
  sample.spineIndex = spineIndex;
  return true;
}

template <typename Reader>
bool readSpineSample(Reader& reader, const uint64_t fileSize, const uint32_t lutOffset, const uint16_t spineCount,
                     const uint64_t spineEntriesStart, const uint16_t index, SpineSample& sample) {
  uint64_t entryPosition = 0;
  if (!readSpineEntryPosition(reader, fileSize, lutOffset, spineCount, spineEntriesStart, index, entryPosition)) {
    return false;
  }

  uint32_t hrefLength = 0;
  if (!readLength(reader, fileSize, entryPosition, hrefLength)) return false;
  const uint64_t fieldsPosition = static_cast<uint64_t>(entryPosition) + sizeof(uint32_t) + hrefLength;
  uint8_t fields[sizeof(uint32_t) + sizeof(int16_t)];
  if (!readAt(reader, fileSize, fieldsPosition, fields, sizeof(fields))) return false;

  sample.position = entryPosition;
  sample.end = fieldsPosition + sizeof(fields);
  sample.cumulativeSize = readLe32(fields);
  sample.tocIndex = readSignedLe16(fields + sizeof(uint32_t));
  return true;
}

template <typename Reader>
void tryReadChapterTitle(Reader& reader, const uint64_t fileSize, const uint32_t lutOffset, const uint16_t spineCount,
                         const uint16_t tocCount, const uint16_t currentSpine, const int16_t tocIndex,
                         const uint64_t tocEntriesStart, const uint64_t lastSpineEntryEnd, const bool hasFirstTocSample,
                         const TocSample& firstTocSample, const uint64_t firstTocNextPosition, char* chapterTitle,
                         const size_t chapterTitleCapacity) {
  if (!chapterTitle || chapterTitleCapacity == 0 || !hasFirstTocSample || tocIndex < 0 || tocIndex >= tocCount) {
    return;
  }

  TocSample sample;
  uint64_t entryLimit = fileSize;
  if (tocIndex == 0) {
    sample = firstTocSample;
    entryLimit = firstTocNextPosition;
  } else {
    uint64_t entryPosition = firstTocNextPosition;
    if (tocIndex != 1 && !readTocEntryPosition(reader, fileSize, lutOffset, spineCount, tocCount, tocEntriesStart,
                                               static_cast<uint16_t>(tocIndex), entryPosition)) {
      return;
    }
    if (entryPosition < tocEntriesStart || entryPosition < lastSpineEntryEnd || entryPosition >= fileSize) return;
    if (tocIndex + 1 < tocCount &&
        (!readTocEntryPosition(reader, fileSize, lutOffset, spineCount, tocCount, tocEntriesStart,
                               static_cast<uint16_t>(tocIndex + 1), entryLimit) ||
         entryLimit <= entryPosition)) {
      return;
    }
    if (!readTocSample(reader, fileSize, tocEntriesStart, spineCount, entryPosition, entryLimit, sample)) return;
  }

  if (sample.position < lastSpineEntryEnd || sample.spineIndex < 0 || sample.spineIndex > currentSpine ||
      sample.titleLength == 0 || sample.titleLength > MAX_PROGRESS_CHAPTER_TITLE_BYTES ||
      sample.titleLength >= chapterTitleCapacity) {
    return;
  }
  uint8_t title[MAX_PROGRESS_CHAPTER_TITLE_BYTES];
  if (!readAt(reader, entryLimit, sample.titlePosition, title, sample.titleLength) ||
      !isValidUtf8(title, sample.titleLength)) {
    return;
  }

  std::memcpy(chapterTitle, title, sample.titleLength);
  chapterTitle[sample.titleLength] = '\0';
}

}  // namespace detail

// Reads a constant number of sampled spine/TOC records and immediate LUT boundaries. TOC failures
// leave a valid percentage intact because the chapter label is optional.
template <typename Reader>
bool readBookBinProgressSnapshot(Reader& reader, const uint8_t expectedVersion, const uint16_t savedSpine,
                                 const uint16_t page, const uint16_t totalPages, const bool includeChapter,
                                 int& percent, char* chapterTitle, const size_t chapterTitleCapacity) {
  percent = -1;
  if (chapterTitle && chapterTitleCapacity > 0) chapterTitle[0] = '\0';

  const uint64_t fileSize = reader.fileSize64();
  uint8_t header[9];
  if (fileSize < sizeof(header) || !detail::readAt(reader, fileSize, 0, header, sizeof(header)) ||
      header[0] != expectedVersion) {
    return false;
  }

  const uint32_t lutOffset = detail::readLe32(header + 1);
  const uint16_t spineCount = detail::readLe16(header + 5);
  const uint16_t tocCount = detail::readLe16(header + 7);
  if (spineCount == 0 || savedSpine > spineCount) return false;

  uint64_t metadataPosition = sizeof(header);
  for (int i = 0; i < 5; ++i) {
    uint32_t length = 0;
    if (!detail::readLength(reader, fileSize, metadataPosition, length)) return false;
    metadataPosition += sizeof(uint32_t) + length;
  }
  if (metadataPosition != lutOffset) return false;

  const uint64_t spineLutSize = static_cast<uint64_t>(spineCount) * sizeof(uint32_t);
  const uint64_t tocLutSize = static_cast<uint64_t>(tocCount) * sizeof(uint32_t);
  const uint64_t spineEntriesStart = static_cast<uint64_t>(lutOffset) + spineLutSize + tocLutSize;
  if (spineEntriesStart > fileSize) return false;

  uint64_t firstTocPosition = fileSize;
  uint64_t firstTocNextPosition = fileSize;
  detail::TocSample firstTocSample;
  bool hasFirstTocSample = false;
  bool hasFirstTocBoundary = tocCount == 0;
  if (tocCount > 0) {
    uint64_t candidateFirstTocPosition = 0;
    uint64_t candidateNextTocPosition = fileSize;
    const bool hasFirstTocPosition = detail::readTocEntryPosition(reader, fileSize, lutOffset, spineCount, tocCount,
                                                                  spineEntriesStart, 0, candidateFirstTocPosition);
    const bool hasNextTocPosition =
        tocCount == 1 || detail::readTocEntryPosition(reader, fileSize, lutOffset, spineCount, tocCount,
                                                      spineEntriesStart, 1, candidateNextTocPosition);
    if (hasFirstTocPosition && hasNextTocPosition &&
        detail::readTocSample(reader, fileSize, spineEntriesStart, spineCount, candidateFirstTocPosition,
                              candidateNextTocPosition, firstTocSample)) {
      firstTocPosition = candidateFirstTocPosition;
      firstTocNextPosition = candidateNextTocPosition;
      hasFirstTocSample = true;
      hasFirstTocBoundary = true;
    }
  }

  detail::SpineSample lastSpine;
  if (!detail::readSpineSample(reader, fileSize, lutOffset, spineCount, spineEntriesStart,
                               static_cast<uint16_t>(spineCount - 1), lastSpine) ||
      lastSpine.cumulativeSize == 0 ||
      (hasFirstTocBoundary && (lastSpine.position >= firstTocPosition || lastSpine.end > firstTocPosition))) {
    return false;
  }

  if (savedSpine == spineCount) {
    percent = 100;
    return true;
  }

  detail::SpineSample currentSpine;
  detail::SpineSample previousSpine;
  const bool hasPrevious = savedSpine > 0;
  if (savedSpine == spineCount - 1) {
    currentSpine = lastSpine;
  } else if (!detail::readSpineSample(reader, fileSize, lutOffset, spineCount, spineEntriesStart, savedSpine,
                                      currentSpine)) {
    return false;
  }
  if (hasPrevious && !detail::readSpineSample(reader, fileSize, lutOffset, spineCount, spineEntriesStart,
                                              static_cast<uint16_t>(savedSpine - 1), previousSpine)) {
    return false;
  }

  if (savedSpine + 1 < spineCount) {
    const uint16_t nextSpineIndex = static_cast<uint16_t>(savedSpine + 1);
    uint64_t nextSpinePosition = lastSpine.position;
    if (nextSpineIndex != spineCount - 1 &&
        !detail::readSpineEntryPosition(reader, fileSize, lutOffset, spineCount, spineEntriesStart, nextSpineIndex,
                                        nextSpinePosition)) {
      return false;
    }
    if (nextSpinePosition <= currentSpine.position || currentSpine.end > nextSpinePosition ||
        (nextSpineIndex != spineCount - 1 && nextSpinePosition >= lastSpine.position) ||
        (hasFirstTocBoundary && nextSpinePosition >= firstTocPosition)) {
      return false;
    }
  }

  const uint32_t previousCumulative = hasPrevious ? previousSpine.cumulativeSize : 0;
  if (previousCumulative > currentSpine.cumulativeSize || currentSpine.cumulativeSize > lastSpine.cumulativeSize ||
      (hasPrevious && (previousSpine.position >= currentSpine.position || previousSpine.end > currentSpine.position)) ||
      currentSpine.position > lastSpine.position ||
      (currentSpine.position != lastSpine.position && currentSpine.end > lastSpine.position)) {
    return false;
  }

  const float fraction = progressFraction(page, totalPages);
  const float bookProgress = calculateProgressFromCumulative(previousCumulative, currentSpine.cumulativeSize,
                                                             lastSpine.cumulativeSize, fraction);
  percent = std::clamp(static_cast<int>(bookProgress * 100.0f + 0.5f), 0, 100);

  if (includeChapter) {
    detail::tryReadChapterTitle(reader, fileSize, lutOffset, spineCount, tocCount, savedSpine, currentSpine.tocIndex,
                                spineEntriesStart, lastSpine.end, hasFirstTocSample, firstTocSample,
                                firstTocNextPosition, chapterTitle, chapterTitleCapacity);
  }
  return true;
}

}  // namespace epub
