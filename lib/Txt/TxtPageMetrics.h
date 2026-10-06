#pragma once

#include <algorithm>

// Small, renderer-independent helpers for the vertical geometry of a TXT page.
class TxtPageMetrics final {
 public:
  static int effectiveLineAdvance(const int rendererLineAdvance) { return std::max(1, rendererLineAdvance); }

  static int linesPerPage(const int viewportHeight, const int lineAdvance) {
    return std::max(1, viewportHeight / effectiveLineAdvance(lineAdvance));
  }
};
