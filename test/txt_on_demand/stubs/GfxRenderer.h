#pragma once

#include <cstddef>
#include <cstdint>

enum class EpdFontFamily { REGULAR };

class GfxRenderer {
 public:
  int getLineHeight(int) const { return 16; }
  int getLineHeight(int, float compression) const { return static_cast<int>(16 * compression + 0.5f); }

  bool isSdCardFont(int) const { return false; }
  bool ensureSdCardFontReady(int, const char*, uint8_t) const { return true; }

  int getTextAdvanceX(int, const char* text, EpdFontFamily) const {
    int width = 0;
    const auto* bytes = reinterpret_cast<const uint8_t*>(text);
    while (*bytes != 0) {
      const uint8_t lead = *bytes;
      size_t expected = 1;
      if ((lead & 0xE0) == 0xC0) expected = 2;
      if ((lead & 0xF0) == 0xE0) expected = 3;
      if ((lead & 0xF8) == 0xF0) expected = 4;

      bool valid = expected > 1;
      for (size_t i = 1; i < expected; ++i) {
        if (bytes[i] == 0 || (bytes[i] & 0xC0) != 0x80) {
          valid = false;
          break;
        }
      }
      if (!valid) expected = 1;
      width += expected == 1 && lead < 0x80 ? 1 : 2;
      bytes += expected;
    }
    return width;
  }
};
