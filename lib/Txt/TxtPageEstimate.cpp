#include "TxtPageEstimate.h"

#include <limits>

void TxtPageEstimate::reset() {
  averageBytesPerPage = INITIAL_BYTES_PER_PAGE;
  sampledThroughOffset = 0;
  hasSampledPage = false;
}

bool TxtPageEstimate::observePage(const size_t pageStartOffset, const size_t pageEndOffset) {
  if (pageEndOffset <= pageStartOffset) return false;

  if (!hasSampledPage) {
    hasSampledPage = true;
  } else {
    // The reader advances through contiguous byte ranges. Reject older or
    // out-of-order spans so Back and replay never count the same page twice.
    if (pageStartOffset != sampledThroughOffset) return false;
  }

  const uint64_t sample = pageEndOffset - pageStartOffset;
  const uint64_t lowLimit = averageBytesPerPage / 2 > 0 ? averageBytesPerPage / 2 : 1;
  const uint64_t highLimit = averageBytesPerPage > std::numeric_limits<uint64_t>::max() / 2
                                 ? std::numeric_limits<uint64_t>::max()
                                 : averageBytesPerPage * 2;
  const uint64_t boundedSample = sample < lowLimit ? lowLimit : (sample > highLimit ? highLimit : sample);

  // A 1/8 step and a 2x sample clamp cap a single page's influence. Round a
  // non-zero difference up to one byte so small estimates still converge.
  if (boundedSample > averageBytesPerPage) {
    uint64_t step = (boundedSample - averageBytesPerPage) / 8;
    if (step == 0) step = 1;
    averageBytesPerPage += step;
  } else if (boundedSample < averageBytesPerPage) {
    uint64_t step = (averageBytesPerPage - boundedSample) / 8;
    if (step == 0) step = 1;
    averageBytesPerPage -= step;
  }

  sampledThroughOffset = pageEndOffset;
  return true;
}

size_t TxtPageEstimate::pageNumberAt(const size_t byteOffset) const {
  if (byteOffset == 0 || averageBytesPerPage == 0) return 1;

  const uint64_t pageIndex = static_cast<uint64_t>(byteOffset) / averageBytesPerPage;
  const uint64_t pageNumber = pageIndex == std::numeric_limits<uint64_t>::max() ? pageIndex : pageIndex + 1;
  return static_cast<size_t>(pageNumber > std::numeric_limits<size_t>::max() ? std::numeric_limits<size_t>::max()
                                                                             : pageNumber);
}

size_t TxtPageEstimate::totalPagesFor(const size_t fileSize) const {
  if (fileSize == 0 || averageBytesPerPage == 0) return 0;

  const uint64_t size = fileSize;
  const uint64_t pageCount = size / averageBytesPerPage + (size % averageBytesPerPage != 0 ? 1 : 0);
  return static_cast<size_t>(pageCount > std::numeric_limits<size_t>::max() ? std::numeric_limits<size_t>::max()
                                                                            : pageCount);
}

size_t TxtPageEstimate::totalPagesFrom(const size_t currentPageNumber, const size_t fileSize,
                                       const size_t visiblePageOffset, const bool currentPageLoaded) const {
  const size_t remainingBytes = fileSize > visiblePageOffset ? fileSize - visiblePageOffset : 0;
  size_t remainingPages = totalPagesFor(remainingBytes);
  if (!currentPageLoaded && remainingBytes > 0) {
    remainingPages = remainingPages > 1 ? remainingPages - 1 : 1;
  }
  const size_t maximum = std::numeric_limits<size_t>::max();
  if (remainingPages > maximum - currentPageNumber) return maximum;
  return currentPageNumber + remainingPages;
}
