#pragma once

#include <cstddef>

#include "TxtPageEstimate.h"
#include "TxtPageReader.h"
#include "TxtProgressFile.h"

namespace txtlayout {

// Drop pagination-dependent state while keeping the exact visible source-byte
// anchor. The returned checkpoint intentionally has no old ordinal/history.
inline TxtProgressState invalidateAtVisibleAnchor(TxtPageHistory& pageHistory, TxtPageEstimate& pageEstimate,
                                                  const size_t anchor, size_t& displayOrdinal,
                                                  bool& hasDisplayOrdinal) {
  pageHistory.reset(anchor);
  pageEstimate.reset();
  displayOrdinal = 0;
  hasDisplayOrdinal = false;

  TxtProgressState resetState;
  resetState.pageStartOffset = anchor;
  return resetState;
}

}  // namespace txtlayout
