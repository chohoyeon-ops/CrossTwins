#pragma once
#include <EpdFontData.h>
class FontDecompressor { public:
 void clearCache() {} void logStats(const char*) {} void resetStats() {}
 int prewarmCache(const EpdFontData*, const char*) { return 0; }
 const uint8_t* getBitmap(const EpdFontData*, const EpdGlyph*, uint32_t) { return nullptr; }
};
