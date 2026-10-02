#pragma once
#include <cstdint>
class CrossPointSettings {
 public:
  enum { JUSTIFIED=0, LEFT_ALIGN=1, CENTER_ALIGN=2, RIGHT_ALIGN=3, BOOK_STYLE=4 };
  uint8_t fontPointSize=12, lineSpacing=1, screenMargin=0, extraParagraphSpacing=0, paragraphIndentSpaces=0, paragraphAlignment=LEFT_ALIGN;
  bool textAntiAliasing=false, hyphenationEnabled=false;
  int getReaderFontId() const { return 42; }
  float getReaderLineCompression() const {
    constexpr float compression[] = {0.8f, 1.0f, 1.3f, 1.6f};
    return compression[lineSpacing < 4 ? lineSpacing : 1];
  }
  struct StatusSpec { bool showsTitle() const { return false; } };
  StatusSpec statusBarSpec() const { return {}; }
};
inline CrossPointSettings testSettings;
#define SETTINGS testSettings
