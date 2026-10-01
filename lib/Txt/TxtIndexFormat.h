#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace txt {

constexpr uint32_t INDEX_MAGIC = 0x54585449;
constexpr uint8_t INDEX_VERSION_V3 = 3;
constexpr uint8_t INDEX_VERSION_V4 = 4;
constexpr uint8_t INDEX_VERSION_V5 = 5;
constexpr size_t INDEX_HEADER_V3_SIZE = 30;
constexpr size_t INDEX_HEADER_V4_SIZE = 30;
constexpr size_t INDEX_HEADER_V5_SIZE = 35;

struct IndexHeader {
  uint8_t version = 0;
  uint32_t sourceFileSize = 0;
  int32_t viewportWidth = 0;
  int32_t linesPerPage = 0;
  int32_t fontId = 0;
  int32_t screenMargin = 0;
  uint8_t alignment = 0;
  int32_t lineAdvance = 0;
  uint8_t fontPointSize = 0;
  uint32_t pageCount = 0;
  size_t headerSize = 0;
};

struct LayoutKey {
  int32_t viewportWidth = 0;
  int32_t linesPerPage = 0;
  int32_t fontId = 0;
  int32_t screenMargin = 0;
  uint8_t alignment = 0;
  int32_t lineAdvance = 0;
  uint8_t fontPointSize = 0;
};

struct ProgressRecord {
  uint16_t page = 0;
  uint32_t sourceAnchor = 0;
  bool hasAnchorRecord = false;
  bool hasSourceAnchor = false;
};

inline uint32_t readLe32(const uint8_t* data) {
  return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}

inline int32_t readLeI32(const uint8_t* data) {
  const uint32_t bits = readLe32(data);
  int32_t value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

inline void writeLe32(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
}

inline size_t headerSizeForVersion(uint8_t version) {
  if (version == INDEX_VERSION_V3) return INDEX_HEADER_V3_SIZE;
  if (version == INDEX_VERSION_V4) return INDEX_HEADER_V4_SIZE;
  if (version == INDEX_VERSION_V5) return INDEX_HEADER_V5_SIZE;
  return 0;
}

inline bool parseIndexHeader(const uint8_t* data, size_t dataSize, size_t indexFileSize, IndexHeader& header) {
  if (!data || dataSize < 5 || readLe32(data) != INDEX_MAGIC) return false;
  const uint8_t version = data[4];
  const size_t headerSize = headerSizeForVersion(version);
  if (headerSize == 0 || dataSize < headerSize || indexFileSize < headerSize) return false;

  IndexHeader parsed;
  parsed.version = version;
  parsed.sourceFileSize = readLe32(data + 5);
  parsed.viewportWidth = readLeI32(data + 9);
  parsed.linesPerPage = readLeI32(data + 13);
  parsed.fontId = readLeI32(data + 17);
  parsed.screenMargin = readLeI32(data + 21);
  parsed.alignment = data[25];
  if (version == INDEX_VERSION_V5) {
    parsed.lineAdvance = readLeI32(data + 26);
    parsed.fontPointSize = data[30];
    parsed.pageCount = readLe32(data + 31);
  } else {
    parsed.pageCount = readLe32(data + 26);
  }
  parsed.headerSize = headerSize;

  const uint64_t maxPagesForSource = static_cast<uint64_t>(parsed.sourceFileSize) + 1;
  if (parsed.pageCount == 0 || parsed.pageCount > (indexFileSize - headerSize) / sizeof(uint32_t) ||
      parsed.pageCount > maxPagesForSource) {
    return false;
  }
  header = parsed;
  return true;
}

inline bool matchesLayout(const IndexHeader& header, const LayoutKey& key) {
  return header.version == INDEX_VERSION_V5 && header.viewportWidth == key.viewportWidth &&
         header.linesPerPage == key.linesPerPage && header.fontId == key.fontId &&
         header.screenMargin == key.screenMargin && header.alignment == key.alignment &&
         header.lineAdvance == key.lineAdvance && header.fontPointSize == key.fontPointSize;
}

inline bool parseProgressRecord(const uint8_t* data, size_t dataSize, uint32_t sourceFileSize,
                                ProgressRecord& record) {
  if (!data || dataSize < 4) return false;
  ProgressRecord parsed;
  parsed.page = static_cast<uint16_t>(data[0] | (uint16_t(data[1]) << 8));
  if (dataSize >= 8) {
    parsed.hasAnchorRecord = true;
    parsed.sourceAnchor = readLe32(data + 4);
    parsed.hasSourceAnchor = parsed.sourceAnchor <= sourceFileSize;
  }
  record = parsed;
  return true;
}

inline void writeProgressRecord(uint8_t* data, uint16_t page, uint32_t sourceAnchor) {
  data[0] = static_cast<uint8_t>(page);
  data[1] = static_cast<uint8_t>(page >> 8);
  data[2] = 0;
  data[3] = 0;
  writeLe32(data + 4, sourceAnchor);
}

template <typename ReadPageOffset, typename IsUtf8Boundary>
bool deriveLegacyPageAnchor(const IndexHeader& header, uint32_t page, uint32_t sourceFileSize,
                            ReadPageOffset readPageOffset, IsUtf8Boundary isUtf8Boundary, uint32_t& sourceAnchor) {
  if ((header.version != INDEX_VERSION_V3 && header.version != INDEX_VERSION_V4) ||
      header.sourceFileSize != sourceFileSize || page >= header.pageCount) {
    return false;
  }

  uint32_t current = 0;
  if (!readPageOffset(page, current)) return false;
  if (sourceFileSize == 0) {
    if (page != 0 || header.pageCount != 1 || current != 0) return false;
  } else if (current >= sourceFileSize) {
    return false;
  }

  if (page == 0) {
    if (current != 0) return false;
  } else {
    uint32_t previous = 0;
    if (!readPageOffset(page - 1, previous) || previous >= current) return false;
  }

  if (page + 1 < header.pageCount) {
    uint32_t next = 0;
    if (!readPageOffset(page + 1, next) || current >= next || next >= sourceFileSize) return false;
  }
  if (!isUtf8Boundary(current)) return false;
  sourceAnchor = current;
  return true;
}

}  // namespace txt
