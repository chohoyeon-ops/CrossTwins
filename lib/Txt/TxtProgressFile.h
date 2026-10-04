#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class Txt;

struct TxtProgressState final {
  static constexpr size_t PREVIOUS_PAGE_CAPACITY = 3;

  size_t pageStartOffset = 0;
  bool hasDisplayOrdinal = false;
  size_t displayOrdinal = 0;
  // Exact starts in chronological order: oldest retained page first, newest last.
  std::array<size_t, PREVIOUS_PAGE_CAPACITY> previousPageStarts{};
  size_t previousPageCount = 0;
};

// Stores source-byte page starts separately from legacy ordinal progress.bin records.
class TxtProgressFile final {
  bool loaded = false;
  bool hasCheckpoint = false;
  uint8_t activeSlot = 0;
  uint32_t generation = 0;
  TxtProgressState savedState;
  uint16_t savedVersion = 0;
  bool savedModeMetadataValid = false;
  bool savedHyphenationEnabled = false;

 public:
  bool load(const Txt& txt, TxtProgressState& state, bool hyphenationEnabled = false);
  bool save(const Txt& txt, const TxtProgressState& state, bool hyphenationEnabled = false);

  // Compatibility helpers for callers/tests that only use the exact offset.
  bool load(const Txt& txt, size_t& pageStartOffset, bool hyphenationEnabled = false);
  bool save(const Txt& txt, size_t pageStartOffset, bool hyphenationEnabled = false);
};
