#pragma once
#include <cstdint>
class CrossPointSettings {
 public:
  enum { JUSTIFIED=0, LEFT_ALIGN=1, CENTER_ALIGN=2, RIGHT_ALIGN=3, BOOK_STYLE=4 };
  uint8_t fontPointSize=12, screenMargin=0, extraParagraphSpacing=0, paragraphIndentSpaces=0, paragraphAlignment=LEFT_ALIGN;
  bool textAntiAliasing=false;
  int getReaderFontId() const { return 42; }
  float getReaderLineCompression() const { return 1; }
  struct StatusSpec { bool showsTitle() const { return false; } };
  StatusSpec statusBarSpec() const { return {}; }
};
inline CrossPointSettings testSettings;
#define SETTINGS testSettings
