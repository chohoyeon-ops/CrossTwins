#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace epub {

inline float progressFraction(const uint16_t page, const uint16_t totalPages) {
  if (totalPages == 0 || page == UINT16_MAX) return 0.0f;
  return std::clamp(static_cast<float>(page) / totalPages, 0.0f, 1.0f);
}

inline float calculateProgressFromCumulative(const size_t previousCumulative, const size_t currentCumulative,
                                             const size_t bookSize, const float currentSpineRead) {
  if (bookSize == 0) return 0.0f;
  const size_t currentSpineSize = currentCumulative - previousCumulative;
  const float currentSpineProgress = currentSpineRead * static_cast<float>(currentSpineSize);
  const float totalProgress = static_cast<float>(previousCumulative) + currentSpineProgress;
  return totalProgress / static_cast<float>(bookSize);
}

}  // namespace epub
