#pragma once

// Host-test stub for FreeInkFont's PSRAM-preferring helpers (FontPsram.h):
// plain heap on the host, same contracts (psramNewArray is nothrow, nullptr
// on OOM). Allocation goes through nothrow operator new[] so the test's
// fail-next-allocation override (see SdCardFontTest.cpp) can inject OOM.
#include <new>
#include <cstdint>
#include <type_traits>
#include <vector>

inline size_t sdFontTestArrayAllocations = 0;
inline size_t sdFontTestKernMatrixAllocations = 0;
inline bool sdFontTestFailArrayAllocations = false;
inline size_t sdFontTestFailNextArrayBytes = 0;

namespace freeink {
namespace font {

template <typename T>
using PsramVector = std::vector<T>;

template <typename T>
T* psramNewArray(std::size_t n) {
  ++sdFontTestArrayAllocations;
  if constexpr (std::is_same_v<T, int8_t>) ++sdFontTestKernMatrixAllocations;
  if (sdFontTestFailArrayAllocations) return nullptr;
  if (sdFontTestFailNextArrayBytes != 0 && sizeof(T) * n == sdFontTestFailNextArrayBytes) {
    sdFontTestFailNextArrayBytes = 0;
    return nullptr;
  }
  return new (std::nothrow) T[n ? n : 1];
}

template <typename T>
void psramDeleteArray(T* p) {
  delete[] p;
}

}  // namespace font
}  // namespace freeink
