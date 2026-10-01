#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace txt {

// Page-index storage with explicit allocation failure; offsets retain their on-disk format.
class PageOffsets {
 public:
  bool reserve(size_t requested) {
    if (requested <= capacity) return true;
    if (requested > SIZE_MAX / sizeof(size_t)) return false;
    auto replacement = std::unique_ptr<size_t[]>(new (std::nothrow) size_t[requested]);
    if (!replacement) return false;
    if (count != 0) std::copy(begin(), end(), replacement.get());
    offsets.swap(replacement);
    capacity = requested;
    return true;
  }
  bool push_back(size_t value) {
    if (count == capacity && (capacity > SIZE_MAX / 2 || !reserve(capacity == 0 ? 16 : capacity * 2))) return false;
    offsets[count++] = value;
    return true;
  }
  void clear() { count = 0; }
  bool empty() const { return count == 0; }
  size_t size() const { return count; }
  size_t back() const { return offsets[count - 1]; }
  size_t operator[](size_t index) const { return offsets[index]; }
  const size_t* begin() const { return offsets.get(); }
  const size_t* end() const { return count == 0 ? offsets.get() : offsets.get() + count; }
  void swap(PageOffsets& other) {
    offsets.swap(other.offsets);
    std::swap(count, other.count);
    std::swap(capacity, other.capacity);
  }
 private:
  std::unique_ptr<size_t[]> offsets;
  size_t count = 0;
  size_t capacity = 0;
};

}  // namespace txt
