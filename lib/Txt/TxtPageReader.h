#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GfxRenderer;
class Txt;

class TxtPageHistory {
 public:
  static constexpr size_t CAPACITY = 256;

  void reset(size_t startingOffset = 0);
  // Seed a resumed history with exact starts ordered oldest-to-newest, then current.
  bool resetWithPrevious(const std::array<size_t, 3>& previousOffsets, size_t previousCount, size_t currentOffset);
  // Copy the closest retained previous starts in chronological order.
  size_t copyPreviousOffsets(std::array<size_t, 3>& previousOffsets) const;
  [[nodiscard]] size_t currentPage() const;
  [[nodiscard]] size_t currentOffset() const;
  [[nodiscard]] bool hasForward() const;
  // Append a newly parsed page start when positioned at the newest retained entry.
  bool advance(size_t nextOffset);
  // Replay the exact retained next start.
  bool forward();
  bool retreat();

 private:
  std::array<size_t, CAPACITY> offsets{};
  size_t head = 0;
  size_t count = 0;
  size_t cursor = 0;
  size_t firstPage = 0;
};

class TxtPageReader final {
  std::unique_ptr<uint8_t[]> sourceBuffer;
  size_t sourceBufferCapacity = 0;

 public:
  static constexpr size_t READ_WINDOW_SIZE = 8 * 1024;

  bool loadPage(const Txt& txt, const GfxRenderer& renderer, int fontId, int viewportWidth, int linesPerPage,
                size_t offset, std::vector<std::string>& outLines, size_t& nextOffset, bool hyphenationEnabled = false);
};
