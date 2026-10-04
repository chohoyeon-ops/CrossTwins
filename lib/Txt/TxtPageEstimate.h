#pragma once

#include <cstddef>
#include <cstdint>

// Display-only page-count estimate learned from page spans already read by the
// TXT reader. This class has no source or persistence dependencies.
class TxtPageEstimate final {
  // Fixed cold/resume prior; bound the first span so a blank or unusually long
  // page cannot dominate the estimate.
  static constexpr uint64_t INITIAL_BYTES_PER_PAGE = 2 * 1024;

  uint64_t averageBytesPerPage = INITIAL_BYTES_PER_PAGE;
  size_t sampledThroughOffset = 0;
  bool hasSampledPage = false;

 public:
  void reset();

  // The first visible page refines the prior. Later samples must extend the
  // sampled frontier contiguously, so Back and forward-history replay do not
  // affect it. Each accepted update is O(1).
  bool observePage(size_t pageStartOffset, size_t pageEndOffset);

  [[nodiscard]] uint64_t bytesPerPage() const { return averageBytesPerPage; }
  [[nodiscard]] size_t pageNumberAt(size_t byteOffset) const;
  [[nodiscard]] size_t totalPagesFor(size_t fileSize) const;
  // Pass the current page end when loaded, or its start with currentPageLoaded=false.
  [[nodiscard]] size_t totalPagesFrom(size_t currentPageNumber, size_t fileSize, size_t visiblePageOffset,
                                      bool currentPageLoaded) const;
};
