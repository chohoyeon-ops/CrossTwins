#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace txt {

struct PageLine {
  std::string text;
  size_t sourceStart = 0;
  size_t sourceEnd = 0;
  bool paragraphStart = false;
  bool paragraphEnd = false;
};

class PageLayout {
 public:
  static constexpr size_t CHUNK_SIZE = 8 * 1024;
  static constexpr size_t READ_AHEAD = 5;

  struct Geometry {
    int viewportHeight = -1;
    int lineAdvance = 1;
    int paragraphGap = 0;
    int paragraphIndentWidth = 0;
    enum class Alignment : uint8_t { JUSTIFIED = 0, LEFT = 1, CENTER = 2, RIGHT = 3, BOOK_STYLE = 4 };
    Alignment paragraphAlignment = Alignment::JUSTIFIED;
    bool (*startsWithRtl)(const char*) = nullptr;
  };

  static int effectiveLineAdvance(int rendererAdvance) { return std::max(1, rendererAdvance); }

  static int indentWidthForSpaces(int fontSpaceWidth, uint8_t spaces) {
    return std::max(0, fontSpaceWidth) * spaces;
  }

  static int gapBeforeLine(bool paragraphStart, bool firstLineOnPage, int paragraphGap) {
    return paragraphStart && !firstLineOnPage ? std::max(0, paragraphGap) : 0;
  }

  static int gapBeforeLine(const PageLine& line, bool firstLineOnPage, int paragraphGap) {
    return gapBeforeLine(line.paragraphStart, firstLineOnPage, paragraphGap);
  }

  static bool isNaturalAlignment(Geometry::Alignment alignment, bool isRtl) {
    if (alignment == Geometry::Alignment::BOOK_STYLE) return !isRtl;
    if (isRtl && (alignment == Geometry::Alignment::LEFT || alignment == Geometry::Alignment::JUSTIFIED)) {
      alignment = Geometry::Alignment::RIGHT;
    }
    return alignment == Geometry::Alignment::JUSTIFIED ||
           (isRtl ? alignment == Geometry::Alignment::RIGHT : alignment == Geometry::Alignment::LEFT);
  }

  static int indentForLine(bool paragraphStart, int paragraphIndentWidth, Geometry::Alignment alignment, bool isRtl) {
    return paragraphStart && isNaturalAlignment(alignment, isRtl) ? std::max(0, paragraphIndentWidth) : 0;
  }

  static int indentForLine(const PageLine& line, int paragraphIndentWidth, Geometry::Alignment alignment, bool isRtl) {
    return indentForLine(line.paragraphStart, paragraphIndentWidth, alignment, isRtl);
  }

  static int indentForText(bool paragraphStart, const char* text, int paragraphIndentWidth,
                           const Geometry& geometry) {
    const bool directionSensitive = geometry.paragraphAlignment == Geometry::Alignment::RIGHT ||
                                    geometry.paragraphAlignment == Geometry::Alignment::BOOK_STYLE;
    const bool isRtl = directionSensitive && geometry.startsWithRtl && geometry.startsWithRtl(text);
    return indentForLine(paragraphStart, paragraphIndentWidth, geometry.paragraphAlignment, isRtl);
  }

  static int calculateLinesPerPage(int viewportHeight, int lineAdvance) {
    return std::max(1, viewportHeight / effectiveLineAdvance(lineAdvance));
  }

  static size_t pageForSourceAnchor(const std::vector<size_t>& pageOffsets, size_t sourceAnchor) {
    if (pageOffsets.empty()) return 0;
    const auto page = std::upper_bound(pageOffsets.begin(), pageOffsets.end(), sourceAnchor);
    return page == pageOffsets.begin() ? 0 : static_cast<size_t>(page - pageOffsets.begin() - 1);
  }

  struct ReadWindow {
    size_t offset;
    size_t length;
    size_t pagePrefix;
  };

  static ReadWindow readWindow(size_t pageOffset, size_t fileSize) {
    const size_t offset = pageOffset > 0 ? pageOffset - 1 : pageOffset;
    const size_t pagePrefix = pageOffset - offset;
    const size_t length = std::min(CHUNK_SIZE + READ_AHEAD + pagePrefix, fileSize - offset);
    return {offset, length, pagePrefix};
  }

  // Source ranges partition consumed bytes; wrap spaces and LF/CRLF belong to
  // the line that consumes them.
  template <typename TextAdvance>
  static bool loadPage(const uint8_t* data, size_t dataOffset, size_t dataSize, size_t pageOffset, size_t fileSize,
                       int maxLines, int viewportWidth, bool startsParagraph, TextAdvance textAdvance,
                       std::vector<PageLine>& lines, size_t& nextOffset) {
    return loadPage(data, dataOffset, dataSize, pageOffset, fileSize, maxLines, viewportWidth, startsParagraph,
                    textAdvance, lines, nextOffset, Geometry{maxLines, 1, 0, 0});
  }

  template <typename TextAdvance>
  static bool loadPage(const uint8_t* data, size_t dataOffset, size_t dataSize, size_t pageOffset, size_t fileSize,
                       int maxLines, int viewportWidth, bool startsParagraph, TextAdvance textAdvance,
                       std::vector<PageLine>& lines, size_t& nextOffset, const Geometry& geometry) {
    lines.clear();
    nextOffset = pageOffset;
    if (!data || pageOffset >= fileSize || dataOffset > pageOffset || maxLines <= 0) return false;
    if (lines.capacity() < static_cast<size_t>(maxLines)) lines.reserve(static_cast<size_t>(maxLines));

    const int lineAdvance = effectiveLineAdvance(geometry.lineAdvance);
    const int viewportHeight = geometry.viewportHeight >= 0 ? geometry.viewportHeight : maxLines * lineAdvance;
    const int paragraphGap = std::max(0, geometry.paragraphGap);
    const int paragraphIndentWidth = std::max(0, geometry.paragraphIndentWidth);
    int usedHeight = 0;

    const size_t pageStart = pageOffset - dataOffset;
    if (pageStart >= dataSize) return false;
    const size_t available = std::min(dataSize - pageStart, CHUNK_SIZE + READ_AHEAD);
    const size_t pageEnd = pageStart + available;
    const size_t chunkEnd = pageStart + std::min(available, CHUNK_SIZE);
    const bool reachesEof = available >= fileSize - pageOffset;
    size_t cursor = pageStart;

    while (cursor < pageEnd && static_cast<int>(lines.size()) < maxLines) {
      if (data[cursor] == '\n') {
        PageLine line;
        line.sourceStart = dataOffset + cursor;
        line.sourceEnd = line.sourceStart + 1;
        line.paragraphStart = startsParagraph;
        line.paragraphEnd = true;
        const int gapBefore = gapBeforeLine(line, lines.empty(), paragraphGap);
        if (!lines.empty() && usedHeight + gapBefore + lineAdvance > viewportHeight) break;
        lines.push_back(std::move(line));
        usedHeight += gapBefore + lineAdvance;
        ++cursor;
        startsParagraph = true;
        continue;
      }

      size_t inputEnd = cursor;
      while (inputEnd < pageEnd && data[inputEnd] != '\n') ++inputEnd;
      const bool hasLf = inputEnd < pageEnd;
      size_t contentEnd = inputEnd;
      size_t terminatorEnd = inputEnd;
      if (hasLf) {
        if (contentEnd > cursor && data[contentEnd - 1] == '\r') --contentEnd;
        terminatorEnd = inputEnd + 1;
      } else if (reachesEof) {
        terminatorEnd = inputEnd;
      }

      // An empty line before LF still represents a paragraph boundary.
      if (cursor == contentEnd) {
        if (!hasLf && !reachesEof) break;
        PageLine line;
        line.sourceStart = dataOffset + cursor;
        line.sourceEnd = dataOffset + terminatorEnd;
        line.paragraphStart = startsParagraph;
        line.paragraphEnd = true;
        const int gapBefore = gapBeforeLine(line, lines.empty(), paragraphGap);
        if (!lines.empty() && usedHeight + gapBefore + lineAdvance > viewportHeight) break;
        lines.push_back(std::move(line));
        usedHeight += gapBefore + lineAdvance;
        cursor = terminatorEnd;
        startsParagraph = true;
        continue;
      }

      bool stopPageAtBoundary = false;
      while (cursor < contentEnd && static_cast<int>(lines.size()) < maxLines) {
        const size_t lineStart = cursor;
        std::string text;
        size_t lastSpace = std::string::npos;
        bool overflow = false;
        bool overflowOnSpace = false;
        bool stoppedAtBoundary = false;
        bool incompleteCodepoint = false;
        while (cursor < contentEnd) {
          if (cursor >= chunkEnd) {
            stoppedAtBoundary = true;
            break;
          }
          const size_t charSize = codepointSize(data, cursor, contentEnd, hasLf || reachesEof);
          if (charSize == 0) {
            incompleteCodepoint = true;
            break;
          }
          if (text.size() + charSize > CHUNK_SIZE + READ_AHEAD && !text.empty()) {
            overflow = true;
            break;
          }

          text.append(reinterpret_cast<const char*>(data + cursor), charSize);
          const int indentWidth = indentForText(startsParagraph, text.c_str(), paragraphIndentWidth, geometry);
          const int width = textAdvance(text.c_str()) + indentWidth;
          if (width > viewportWidth && text.size() > charSize) {
            text.resize(text.size() - charSize);
            overflowOnSpace = charSize == 1 && data[cursor] == ' ';
            overflow = true;
            break;
          }

          cursor += charSize;
          if (width > viewportWidth) {
            // A single glyph wider than the viewport still consumes one whole
            // codepoint so page indexing cannot stall.
            overflow = cursor < contentEnd;
            break;
          }
          if (charSize == 1 && data[cursor - 1] == ' ') lastSpace = text.size() - 1;
        }

        if (cursor == lineStart) {
          stopPageAtBoundary = stoppedAtBoundary || incompleteCodepoint;
          break;
        }

        size_t consumedEnd = cursor;
        if (overflow && lastSpace != std::string::npos && lastSpace > 0) {
          text.resize(lastSpace);
          consumedEnd = lineStart + lastSpace + 1;
          cursor = consumedEnd;
        } else if (overflowOnSpace) {
          consumedEnd = cursor + 1;
          cursor = consumedEnd;
        }

        const bool atInputEnd = cursor == contentEnd;
        const bool paragraphEnd = atInputEnd && (hasLf || reachesEof);
        PageLine line;
        line.text = std::move(text);
        line.sourceStart = dataOffset + lineStart;
        line.sourceEnd = dataOffset + (paragraphEnd ? terminatorEnd : consumedEnd);
        line.paragraphStart = startsParagraph;
        line.paragraphEnd = paragraphEnd;
        const int gapBefore = gapBeforeLine(line, lines.empty(), paragraphGap);
        if (!lines.empty() && usedHeight + gapBefore + lineAdvance > viewportHeight) {
          cursor = lineStart;
          stopPageAtBoundary = true;
          break;
        }
        lines.push_back(std::move(line));
        usedHeight += gapBefore + lineAdvance;
        startsParagraph = false;

        if (stoppedAtBoundary || incompleteCodepoint) {
          stopPageAtBoundary = true;
          break;
        }
        if (paragraphEnd) {
          cursor = terminatorEnd;
          startsParagraph = true;
          break;
        }
      }

      if (stopPageAtBoundary) break;
      if (cursor == contentEnd) {
        if (hasLf) {
          // The final line above owns the complete LF or CRLF byte range.
          startsParagraph = true;
          cursor = terminatorEnd;
        } else if (reachesEof) {
          break;
        } else {
          break;
        }
      }
    }

    if (cursor <= pageStart || lines.empty()) return false;
    nextOffset = dataOffset + cursor;
    if (nextOffset > fileSize) nextOffset = fileSize;
    return nextOffset > pageOffset;
  }

 private:
  static size_t codepointSize(const uint8_t* data, size_t position, size_t end, bool atKnownParagraphEnd) {
    const uint8_t lead = data[position];
    size_t expected = 1;
    if ((lead & 0xE0) == 0xC0) expected = 2;
    else if ((lead & 0xF0) == 0xE0) expected = 3;
    else if ((lead & 0xF8) == 0xF0) expected = 4;
    else return 1;

    if (position + expected > end) return atKnownParagraphEnd ? 1 : 0;
    for (size_t i = 1; i < expected; ++i) {
      if ((data[position + i] & 0xC0) != 0x80) return 1;
    }
    return expected;
  }
};

}  // namespace txt
