#pragma once
#include <HalStorage.h>
#include <SdCardFont.h>
#include <algorithm>
#include <vector>

// Genuine .cpfont v4 bytes read by production SdCardFont through the host HAL.
inline void makeTypographyFont(bool replacement = true, size_t extraHangul = 0, bool specialSpaces = true, uint16_t bitmapBytes = 1, bool kerning = true) {
  std::vector<uint32_t> cps = {' ', '!', ',', '.', '0', '1', '2', 'A', 'V', 'X', 'Z', 'f', 'i', 0x301, 0x627, 0x644,
                               0xA0, 0x3000, 0x4E00, 0xAC00, 0xFB01, 0xFFFD};
  if (!specialSpaces) {
    cps.erase(std::find(cps.begin(), cps.end(), 0xA0));
    cps.erase(std::find(cps.begin(), cps.end(), 0x3000));
  }
  if (!replacement) cps.erase(std::find(cps.begin(), cps.end(), 0xFFFD));
  cps.reserve(cps.size() + 0xFEFC - 0xFE8D + 1 + extraHangul);
  for (size_t i = 1; i <= extraHangul; ++i) cps.push_back(0xAC00 + i);
  for (uint32_t cp = 0xFE8D; cp <= 0xFEFC; ++cp) cps.push_back(cp);
  std::sort(cps.begin(), cps.end());
  const size_t glyphOffset = 64 + cps.size() * 12;
  const size_t kernOffset = glyphOffset + cps.size() * 16;
  const size_t ligOffset = kernOffset + (kerning ? 7 : 0);
  const size_t bitmapOffset = ligOffset + 8;
  sdFontTestFile.assign(bitmapOffset + cps.size() * bitmapBytes, 0);
  const auto put16 = [](size_t at, uint16_t v) {
    sdFontTestFile[at] = v; sdFontTestFile[at + 1] = v >> 8;
  };
  const auto put32 = [&put16](size_t at, uint32_t v) {
    put16(at, v); put16(at + 2, v >> 16);
  };
  std::memcpy(sdFontTestFile.data(), "CPFONT\0\0", 8);
  put16(8, CPFONT_VERSION);
  sdFontTestFile[12] = 1;
  put32(36, cps.size()); put32(40, cps.size());
  sdFontTestFile[44] = 12; put16(45, 8); put16(47, static_cast<uint16_t>(-2));
  put16(49, kerning ? 1 : 0); put16(51, kerning ? 1 : 0);
  sdFontTestFile[53] = kerning ? 1 : 0; sdFontTestFile[54] = kerning ? 1 : 0; sdFontTestFile[55] = 1;
  put32(56, 64);
  for (size_t i = 0; i < cps.size(); ++i) {
    const uint32_t cp = cps[i];
    put32(64 + i*12, cp); put32(68 + i*12, cp); put32(72 + i*12, i);
    EpdGlyph glyph{};
    glyph.width = 1; glyph.height = 1; glyph.top = 1;
    glyph.advanceX = cp == 'X' ? 137 : cp == 0x301 || cp == 'Z' ? 0 : cp == 0xFFFD ? 80 :
                     cp == 'f' || cp == 'i' ? 96 : 128;
    glyph.dataLength = bitmapBytes; glyph.dataOffset = i * bitmapBytes;
    std::memcpy(sdFontTestFile.data() + glyphOffset + i*16, &glyph, sizeof(glyph));
    sdFontTestFile[bitmapOffset + i * bitmapBytes] = 0x80;
  }
  if (kerning) {
  put16(kernOffset, 'A'); sdFontTestFile[kernOffset + 2] = 1;
  put16(kernOffset + 3, 'V'); sdFontTestFile[kernOffset + 5] = 1;
  sdFontTestFile[kernOffset + 6] = static_cast<uint8_t>(-16);
  }
  put32(ligOffset, (static_cast<uint32_t>('f') << 16) | 'i'); put32(ligOffset + 4, 0xFB01);
}
