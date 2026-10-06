#include "TxtProgressFile.h"

#include <HalStorage.h>
#include <Txt.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace {
constexpr size_t V1_RECORD_SIZE = 44;
constexpr size_t V1_CHECKSUM_OFFSET = 40;
constexpr size_t V4_RECORD_SIZE = 84;
constexpr size_t CORE_CHECKSUM_OFFSET = 36;
constexpr size_t METADATA_OFFSET = 40;
constexpr size_t METADATA_CHECKSUM_OFFSET = 80;
constexpr uint8_t MAGIC[4] = {'T', 'X', 'P', 'R'};

struct SerializedRecord {
  std::array<uint8_t, V4_RECORD_SIZE> data{};
  size_t size = 0;
};

struct Checkpoint {
  uint8_t slot = 0;
  uint32_t generation = 0;
  uint16_t version = 0;
  bool modeMetadataValid = false;
  bool hyphenationEnabled = false;
  TxtProgressState state;
};

enum class RecordReadResult { Missing, IoError, Invalid, Valid };

uint16_t readU16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readU32(const uint8_t* bytes) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(bytes[i]) << (i * 8);
  return value;
}

uint64_t readU64(const uint8_t* bytes) {
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
  return value;
}

void writeU16(uint8_t* bytes, const uint16_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(uint8_t* bytes, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) {
    bytes[i] = static_cast<uint8_t>(value);
    value >>= 8;
  }
}

void writeU64(uint8_t* bytes, uint64_t value) {
  for (size_t i = 0; i < 8; ++i) {
    bytes[i] = static_cast<uint8_t>(value);
    value >>= 8;
  }
}

uint32_t checksum(const uint8_t* bytes, const size_t length) {
  uint32_t value = 0xFFFFFFFFu;
  for (size_t i = 0; i < length; ++i) {
    value ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      value = (value >> 1) ^ ((value & 1u) ? 0xEDB88320u : 0u);
    }
  }
  return ~value;
}

uint64_t pathFingerprint(const std::string& path) {
  uint64_t value = 14695981039346656037ull;
  for (const unsigned char byte : path) {
    value ^= byte;
    value *= 1099511628211ull;
  }
  return value;
}

bool isContinuationByte(const uint8_t byte) { return (byte & 0xC0u) == 0x80u; }

RecordReadResult isSourceBoundary(const Txt& txt, const uint64_t offset) {
  const uint64_t fileSize = txt.getFileSize();
  if (offset > fileSize) return RecordReadResult::Invalid;
  if (fileSize == 0) return offset == 0 ? RecordReadResult::Valid : RecordReadResult::Invalid;
  if (offset == fileSize || offset > std::numeric_limits<size_t>::max()) return RecordReadResult::Invalid;
  if (offset == 0) return RecordReadResult::Valid;

  uint8_t byte = 0;
  if (!txt.readContent(&byte, static_cast<size_t>(offset), 1)) return RecordReadResult::IoError;
  return isContinuationByte(byte) ? RecordReadResult::Invalid : RecordReadResult::Valid;
}

std::string slotPath(const Txt& txt, const uint8_t slot, const bool temporary) {
  std::string path = txt.getCachePath();
  path += slot == 0 ? "/progress0.bin" : "/progress1.bin";
  if (temporary) path += ".tmp";
  return path;
}

RecordReadResult readRecord(const std::string& path, SerializedRecord& record) {
  if (!Storage.exists(path.c_str())) return RecordReadResult::Missing;

  HalFile file;
  if (!Storage.openFileForRead("TPR", path, file)) return RecordReadResult::IoError;
  const size_t fileSize = file.size();
  const bool supportedSize = fileSize == V1_RECORD_SIZE || fileSize == V4_RECORD_SIZE;
  const int bytesRead = supportedSize ? file.read(record.data.data(), fileSize) : -1;
  const bool closed = file.close();
  if (!closed) return RecordReadResult::IoError;
  if (!supportedSize) return RecordReadResult::Invalid;
  if (bytesRead != static_cast<int>(fileSize)) return RecordReadResult::IoError;
  record.size = fileSize;
  return RecordReadResult::Valid;
}

bool validOptionalHistory(const TxtProgressState& state, const uint64_t currentOffset) {
  if (state.previousPageCount > TxtProgressState::PREVIOUS_PAGE_CAPACITY) return false;
  if (state.hasDisplayOrdinal && state.previousPageCount >= state.displayOrdinal) return false;

  uint64_t previous = 0;
  for (size_t i = 0; i < state.previousPageCount; ++i) {
    const uint64_t offset = state.previousPageStarts[i];
    if (offset >= currentOffset || (i > 0 && offset <= previous)) return false;
    previous = offset;
  }
  return true;
}

RecordReadResult decodeRecord(const SerializedRecord& record, const Txt& txt, Checkpoint& checkpoint,
                              const uint8_t slot, const bool requestedHyphenationEnabled) {
  const auto& data = record.data;
  if (data[0] != MAGIC[0] || data[1] != MAGIC[1] || data[2] != MAGIC[2] || data[3] != MAGIC[3]) {
    return RecordReadResult::Invalid;
  }

  const uint16_t version = readU16(data.data() + 4);
  const uint16_t recordSize = readU16(data.data() + 6);
  if (version == 1) {
    if (record.size != V1_RECORD_SIZE || recordSize != V1_RECORD_SIZE ||
        checksum(data.data(), V1_CHECKSUM_OFFSET) != readU32(data.data() + V1_CHECKSUM_OFFSET)) {
      return RecordReadResult::Invalid;
    }
  } else if (version == 2 || version == 3 || version == 4) {
    if (record.size != V4_RECORD_SIZE || recordSize != V4_RECORD_SIZE ||
        checksum(data.data(), CORE_CHECKSUM_OFFSET) != readU32(data.data() + CORE_CHECKSUM_OFFSET)) {
      return RecordReadResult::Invalid;
    }
  } else {
    return RecordReadResult::Invalid;
  }

  const uint32_t storedGeneration = readU32(data.data() + 8);
  const uint64_t storedPath = readU64(data.data() + 12);
  const uint64_t storedSize = readU64(data.data() + 20);
  const uint64_t storedOffset = readU64(data.data() + 28);
  if (storedGeneration == 0 || storedPath != pathFingerprint(txt.getPath()) || storedSize != txt.getFileSize()) {
    return RecordReadResult::Invalid;
  }
  if (storedOffset > std::numeric_limits<size_t>::max()) return RecordReadResult::Invalid;
  const auto boundaryResult = isSourceBoundary(txt, storedOffset);
  if (boundaryResult != RecordReadResult::Valid) return boundaryResult;

  checkpoint.slot = slot;
  checkpoint.generation = storedGeneration;
  checkpoint.version = version;
  checkpoint.state = {};
  checkpoint.state.pageStartOffset = static_cast<size_t>(storedOffset);

  // Version 1 is the accepted Phase 1 format. Its exact source offset remains
  // valid, while display metadata falls back to the Phase 2.5 estimator.
  if (version == 1) return RecordReadResult::Valid;

  // Older records describe OFF semantics. When opening with Korean splitting
  // enabled, preserve only their exact source anchor.
  if (version < 4 && requestedHyphenationEnabled) return RecordReadResult::Valid;

  if (checksum(data.data() + METADATA_OFFSET, METADATA_CHECKSUM_OFFSET - METADATA_OFFSET) !=
      readU32(data.data() + METADATA_CHECKSUM_OFFSET)) {
    return RecordReadResult::Valid;
  }

  if (version == 4) {
    const uint8_t storedMode = data[METADATA_OFFSET + 9];
    if (storedMode > 1) return RecordReadResult::Valid;
    checkpoint.modeMetadataValid = true;
    checkpoint.hyphenationEnabled = storedMode != 0;
    if (checkpoint.hyphenationEnabled != requestedHyphenationEnabled) return RecordReadResult::Valid;
  }

  const uint64_t displayOrdinal = readU64(data.data() + METADATA_OFFSET);
  if (displayOrdinal > 0 && displayOrdinal <= std::numeric_limits<size_t>::max() && displayOrdinal <= storedSize) {
    checkpoint.state.hasDisplayOrdinal = true;
    checkpoint.state.displayOrdinal = static_cast<size_t>(displayOrdinal);
  }

  // Version 2 boundaries belong to the earlier TXT wrapping policy. Preserve
  // its exact current anchor and display ordinal, but do not replay its history.
  if (version == 3 || version == 4) {
    const uint8_t previousCount = data[METADATA_OFFSET + 8];
    if (previousCount <= TxtProgressState::PREVIOUS_PAGE_CAPACITY) {
      checkpoint.state.previousPageCount = previousCount;
      bool offsetsFit = true;
      for (size_t i = 0; i < previousCount; ++i) {
        const uint64_t previousOffset = readU64(data.data() + METADATA_OFFSET + 16 + i * 8);
        if (previousOffset > std::numeric_limits<size_t>::max()) {
          offsetsFit = false;
          break;
        }
        checkpoint.state.previousPageStarts[i] = static_cast<size_t>(previousOffset);
      }
      if (!offsetsFit || !validOptionalHistory(checkpoint.state, storedOffset)) {
        checkpoint.state.previousPageStarts.fill(0);
        checkpoint.state.previousPageCount = 0;
      }
    }
  }
  return RecordReadResult::Valid;
}

RecordReadResult readCheckpoint(const Txt& txt, const uint8_t slot, Checkpoint& checkpoint,
                                const bool requestedHyphenationEnabled) {
  SerializedRecord record;
  const auto result = readRecord(slotPath(txt, slot, false), record);
  if (result != RecordReadResult::Valid) return result;
  return decodeRecord(record, txt, checkpoint, slot, requestedHyphenationEnabled);
}

bool generationIsNewer(const uint32_t candidate, const uint32_t current) {
  return static_cast<int32_t>(candidate - current) > 0;
}

TxtProgressState normalizedState(const TxtProgressState& input) {
  TxtProgressState normalized = input;
  if (!normalized.hasDisplayOrdinal || normalized.displayOrdinal == 0) {
    normalized.hasDisplayOrdinal = false;
    normalized.displayOrdinal = 0;
  }
  if (normalized.previousPageCount > TxtProgressState::PREVIOUS_PAGE_CAPACITY ||
      !validOptionalHistory(normalized, normalized.pageStartOffset)) {
    normalized.previousPageStarts.fill(0);
    normalized.previousPageCount = 0;
  }
  for (size_t i = normalized.previousPageCount; i < normalized.previousPageStarts.size(); ++i) {
    normalized.previousPageStarts[i] = 0;
  }
  return normalized;
}

bool sameState(const TxtProgressState& first, const TxtProgressState& second) {
  return first.pageStartOffset == second.pageStartOffset && first.hasDisplayOrdinal == second.hasDisplayOrdinal &&
         first.displayOrdinal == second.displayOrdinal && first.previousPageCount == second.previousPageCount &&
         first.previousPageStarts == second.previousPageStarts;
}

void encodeRecord(const Txt& txt, const uint32_t generation, const TxtProgressState& state,
                  const bool hyphenationEnabled, std::array<uint8_t, V4_RECORD_SIZE>& data) {
  data.fill(0);
  data[0] = MAGIC[0];
  data[1] = MAGIC[1];
  data[2] = MAGIC[2];
  data[3] = MAGIC[3];
  writeU16(data.data() + 4, 4);
  writeU16(data.data() + 6, static_cast<uint16_t>(V4_RECORD_SIZE));
  writeU32(data.data() + 8, generation);
  writeU64(data.data() + 12, pathFingerprint(txt.getPath()));
  writeU64(data.data() + 20, txt.getFileSize());
  writeU64(data.data() + 28, state.pageStartOffset);
  writeU32(data.data() + CORE_CHECKSUM_OFFSET, checksum(data.data(), CORE_CHECKSUM_OFFSET));

  writeU64(data.data() + METADATA_OFFSET, state.hasDisplayOrdinal ? state.displayOrdinal : 0);
  data[METADATA_OFFSET + 8] = static_cast<uint8_t>(state.previousPageCount);
  data[METADATA_OFFSET + 9] = hyphenationEnabled ? 1 : 0;
  for (size_t i = 0; i < state.previousPageCount; ++i) {
    writeU64(data.data() + METADATA_OFFSET + 16 + i * 8, state.previousPageStarts[i]);
  }
  writeU32(data.data() + METADATA_CHECKSUM_OFFSET,
           checksum(data.data() + METADATA_OFFSET, METADATA_CHECKSUM_OFFSET - METADATA_OFFSET));
}

bool writeTempRecord(const std::string& path, const std::array<uint8_t, V4_RECORD_SIZE>& data) {
  HalFile file;
  if (!Storage.openFileForWrite("TPR", path, file)) return false;
  const size_t written = file.write(data.data(), data.size());
  if (written != data.size()) {
    file.close();
    return false;
  }
  file.flush();
  return file.close();
}

bool tempRecordMatches(const std::string& path, const Txt& txt, const std::array<uint8_t, V4_RECORD_SIZE>& expected,
                       const bool hyphenationEnabled) {
  SerializedRecord actual;
  if (readRecord(path, actual) != RecordReadResult::Valid || actual.size != V4_RECORD_SIZE) return false;

  Checkpoint decoded;
  if (decodeRecord(actual, txt, decoded, 0, hyphenationEnabled) != RecordReadResult::Valid) return false;
  return actual.data == expected;
}
}  // namespace

bool TxtProgressFile::load(const Txt& txt, TxtProgressState& state, const bool hyphenationEnabled) {
  loaded = false;
  hasCheckpoint = false;
  generation = 0;
  activeSlot = 0;
  savedState = {};
  savedVersion = 0;
  savedModeMetadataValid = false;
  savedHyphenationEnabled = false;
  state = {};

  Checkpoint first;
  Checkpoint second;
  const auto firstResult = readCheckpoint(txt, 0, first, hyphenationEnabled);
  const auto secondResult = readCheckpoint(txt, 1, second, hyphenationEnabled);
  const bool readError = firstResult == RecordReadResult::IoError || secondResult == RecordReadResult::IoError;
  const bool firstValid = firstResult == RecordReadResult::Valid;
  const bool secondValid = secondResult == RecordReadResult::Valid;
  if (!firstValid && !secondValid) {
    loaded = !readError;
    return false;
  }

  const Checkpoint& newest = firstValid && secondValid
                                 ? (generationIsNewer(second.generation, first.generation) ? second : first)
                                 : (firstValid ? first : second);
  activeSlot = newest.slot;
  generation = newest.generation;
  savedVersion = newest.version;
  savedModeMetadataValid = newest.modeMetadataValid;
  savedHyphenationEnabled = newest.hyphenationEnabled;
  savedState = newest.state;
  hasCheckpoint = true;
  loaded = !readError;
  state = savedState;
  return true;
}

bool TxtProgressFile::load(const Txt& txt, size_t& pageStartOffset, const bool hyphenationEnabled) {
  TxtProgressState state;
  const bool restored = load(txt, state, hyphenationEnabled);
  pageStartOffset = state.pageStartOffset;
  return restored;
}

bool TxtProgressFile::save(const Txt& txt, const TxtProgressState& requestedState, const bool hyphenationEnabled) {
  if (!loaded) {
    TxtProgressState ignored;
    load(txt, ignored, hyphenationEnabled);
  }
  if (!loaded) return false;

  TxtProgressState state = normalizedState(requestedState);
  if (state.hasDisplayOrdinal && state.displayOrdinal > txt.getFileSize()) {
    state.hasDisplayOrdinal = false;
    state.displayOrdinal = 0;
  }
  if (!validOptionalHistory(state, state.pageStartOffset)) {
    state.previousPageStarts.fill(0);
    state.previousPageCount = 0;
  }
  if (isSourceBoundary(txt, state.pageStartOffset) != RecordReadResult::Valid) return false;
  const bool checkpointSemanticsMatch =
      savedVersion == 4 && savedModeMetadataValid && savedHyphenationEnabled == hyphenationEnabled;
  if (hasCheckpoint && checkpointSemanticsMatch && sameState(savedState, state)) return true;

  const uint8_t targetSlot = hasCheckpoint ? static_cast<uint8_t>(activeSlot ^ 1u) : 0;
  const uint32_t nextGeneration = generation + 1u == 0 ? 1u : generation + 1u;
  const auto temporaryPath = slotPath(txt, targetSlot, true);
  const auto targetPath = slotPath(txt, targetSlot, false);

  std::array<uint8_t, V4_RECORD_SIZE> record{};
  encodeRecord(txt, nextGeneration, state, hyphenationEnabled, record);
  if (!writeTempRecord(temporaryPath, record) || !tempRecordMatches(temporaryPath, txt, record, hyphenationEnabled)) {
    return false;
  }

  // The newest valid slot is never removed. Only replace the inactive slot.
  if (hasCheckpoint && Storage.exists(targetPath.c_str()) && !Storage.remove(targetPath.c_str())) return false;
  if (!Storage.rename(temporaryPath.c_str(), targetPath.c_str())) return false;

  activeSlot = targetSlot;
  generation = nextGeneration;
  savedState = state;
  savedVersion = 4;
  savedModeMetadataValid = true;
  savedHyphenationEnabled = hyphenationEnabled;
  hasCheckpoint = true;
  return true;
}

bool TxtProgressFile::save(const Txt& txt, const size_t pageStartOffset, const bool hyphenationEnabled) {
  TxtProgressState state;
  state.pageStartOffset = pageStartOffset;
  return save(txt, state, hyphenationEnabled);
}
