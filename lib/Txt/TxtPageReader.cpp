#include "TxtPageReader.h"

#include <CjkLineBreak.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <Txt.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace {
constexpr size_t CONTINUATION_MASK = 0xC0;
constexpr size_t CONTINUATION_BYTE = 0x80;
constexpr size_t MAX_HANGUL_WORD_BYTES = 200;

int expectedUtf8Length(const uint8_t lead) {
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

bool isContinuation(const uint8_t value) { return (value & CONTINUATION_MASK) == CONTINUATION_BYTE; }

size_t completePrefixLength(const uint8_t* bytes, const size_t length, const bool atEof) {
  if (atEof || length == 0) return length;

  size_t sequenceStart = length - 1;
  while (sequenceStart > 0 && isContinuation(bytes[sequenceStart])) {
    --sequenceStart;
  }

  const int expected = expectedUtf8Length(bytes[sequenceStart]);
  if (expected <= 1) return length;

  const size_t available = length - sequenceStart;
  if (available >= static_cast<size_t>(expected)) return length;
  for (size_t i = sequenceStart + 1; i < length; ++i) {
    if (!isContinuation(bytes[i])) return length;
  }
  return sequenceStart;
}

size_t firstUtf8UnitLength(const std::string& text) {
  if (text.empty()) return 0;

  const int expected = expectedUtf8Length(static_cast<uint8_t>(text[0]));
  if (expected <= 1) return 1;
  size_t availableSequenceBytes = 1;
  while (availableSequenceBytes < static_cast<size_t>(expected) && availableSequenceBytes < text.size() &&
         isContinuation(static_cast<uint8_t>(text[availableSequenceBytes]))) {
    ++availableSequenceBytes;
  }
  return availableSequenceBytes;
}

size_t previousUtf8Boundary(const std::string& text, size_t position) {
  while (position > 0 && position < text.size() && isContinuation(static_cast<uint8_t>(text[position]))) {
    --position;
  }
  return position;
}

bool decodeUtf8(const std::string& text, const size_t start, uint32_t& codepoint, size_t& length) {
  if (start >= text.size()) return false;

  const auto byteAt = [&text](const size_t position) { return static_cast<uint8_t>(text[position]); };
  const uint8_t lead = byteAt(start);
  if (lead < 0x80) {
    codepoint = lead;
    length = 1;
    return true;
  }

  if (lead >= 0xC2 && lead <= 0xDF) {
    length = 2;
    codepoint = lead & 0x1Fu;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    length = 3;
    codepoint = lead & 0x0Fu;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    length = 4;
    codepoint = lead & 0x07u;
  } else {
    return false;
  }

  if (length > text.size() - start) return false;
  for (size_t i = 1; i < length; ++i) {
    const uint8_t continuation = byteAt(start + i);
    if (!isContinuation(continuation)) return false;
    if (i == 1 && ((lead == 0xE0 && continuation < 0xA0) || (lead == 0xED && continuation >= 0xA0) ||
                   (lead == 0xF0 && continuation < 0x90) || (lead == 0xF4 && continuation >= 0x90))) {
      return false;
    }
    codepoint = (codepoint << 6) | (continuation & 0x3Fu);
  }
  return true;
}

bool previousCodepoint(const std::string& text, const size_t end, size_t& start, uint32_t& codepoint) {
  if (end == 0 || end > text.size()) return false;

  size_t candidate = end - 1;
  size_t continuationCount = 0;
  while (candidate > 0 && continuationCount < 3 && isContinuation(static_cast<uint8_t>(text[candidate]))) {
    --candidate;
    ++continuationCount;
  }

  size_t length = 0;
  uint32_t decoded = 0;
  if (!decodeUtf8(text, candidate, decoded, length) || candidate + length != end) return false;
  start = candidate;
  codepoint = decoded;
  return true;
}

bool isAsciiWhitespace(const uint32_t codepoint) {
  return codepoint == ' ' || (codepoint >= '\t' && codepoint <= '\r');
}

bool isNarrowCjkLetter(const uint32_t codepoint) {
  return (codepoint >= 0x1100 && codepoint <= 0x11FF) || (codepoint >= 0x3130 && codepoint <= 0x318F) ||
         (codepoint >= 0xA960 && codepoint <= 0xA97F) || (codepoint >= 0xAC00 && codepoint <= 0xD7FF) ||
         (codepoint >= 0xFFA0 && codepoint <= 0xFFDC) || (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||
         (codepoint >= 0x4E00 && codepoint <= 0x9FFF) || (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
         (codepoint >= 0x20000 && codepoint <= 0x2A6DF) || (codepoint >= 0x3041 && codepoint <= 0x3096) ||
         (codepoint >= 0x309D && codepoint <= 0x309F) || (codepoint >= 0x30A1 && codepoint <= 0x30FA) ||
         (codepoint >= 0x30FC && codepoint <= 0x30FF);
}

bool isNoLineStart(const uint32_t codepoint) {
  switch (codepoint) {
    case '.':
    case ',':
    case ':':
    case ';':
    case '!':
    case '?':
    case ')':
    case ']':
    case '}':
    case 0x2019:
    case 0x201D:
    case 0x3001:
    case 0x3002:
    case 0x3009:
    case 0x300B:
    case 0x300D:
    case 0x300F:
    case 0x3011:
    case 0xFF01:
    case 0xFF09:
    case 0xFF0C:
    case 0xFF0E:
    case 0xFF1A:
    case 0xFF1B:
    case 0xFF1F:
    case 0xFF3D:
    case 0xFF5D:
      return true;
    default:
      return false;
  }
}

bool isNoLineEnd(const uint32_t codepoint) {
  switch (codepoint) {
    case '(':
    case '[':
    case '{':
    case 0x2018:
    case 0x201C:
    case 0x3008:
    case 0x300A:
    case 0x300C:
    case 0x300E:
    case 0x3010:
    case 0xFF08:
    case 0xFF3B:
    case 0xFF5B:
      return true;
    default:
      return false;
  }
}

size_t adjustPunctuationBreak(std::string& line, const size_t breakPos, const int viewportWidth, const int fontId,
                              const GfxRenderer& renderer) {
  if (breakPos == 0 || breakPos >= line.size()) return breakPos;

  size_t leftStart = 0;
  uint32_t left = 0;
  uint32_t right = 0;
  size_t rightLength = 0;
  if (!previousCodepoint(line, breakPos, leftStart, left) || !decodeUtf8(line, breakPos, right, rightLength) ||
      rightLength == 0) {
    return breakPos;
  }
  if (isAsciiWhitespace(left) || isAsciiWhitespace(right)) return breakPos;
  if (!isNarrowCjkLetter(left) && !isNarrowCjkLetter(right)) return breakPos;
  if (!isNoLineEnd(left) && !isNoLineStart(right)) return breakPos;

  size_t replacementLeftStart = 0;
  uint32_t replacementLeft = 0;
  if (leftStart == 0 || !previousCodepoint(line, leftStart, replacementLeftStart, replacementLeft)) return breakPos;
  if (isAsciiWhitespace(replacementLeft) || isAsciiWhitespace(left)) return breakPos;
  if (isNoLineEnd(replacementLeft) || isNoLineStart(left)) return breakPos;

  const char savedByte = line[leftStart];
  line[leftStart] = '\0';
  const int replacementWidth = renderer.getTextAdvanceX(fontId, line.c_str(), EpdFontFamily::REGULAR);
  line[leftStart] = savedByte;
  return replacementWidth <= viewportWidth ? leftStart : breakPos;
}

bool isValidUtf8Range(const std::string& text, const size_t start, const size_t end) {
  size_t position = start;
  while (position < end) {
    uint32_t codepoint = 0;
    size_t length = 0;
    if (!decodeUtf8(text, position, codepoint, length) || length == 0 || length > end - position) return false;
    position += length;
  }
  return position == end;
}

int measurePrefixWithTemporaryEnd(std::string& line, const size_t end, const int fontId, const GfxRenderer& renderer) {
  if (end >= line.size()) return renderer.getTextAdvanceX(fontId, line.c_str(), EpdFontFamily::REGULAR);
  const char savedByte = line[end];
  line[end] = '\0';
  const int width = renderer.getTextAdvanceX(fontId, line.c_str(), EpdFontFamily::REGULAR);
  line[end] = savedByte;
  return width;
}

// Try the EPUB Hangul boundary policy for the single overflowing TXT word.
// The existing 8 KiB window is the only source available to this local adapter.
bool selectHangulLineEndBreak(std::string& line, const size_t baselineBreakPos, const bool logicalLineComplete,
                              const int viewportWidth, const int fontId, const GfxRenderer& renderer,
                              size_t& selectedBreakPos) {
  if (line.find('\0') != std::string::npos || baselineBreakPos > line.size()) return false;

  size_t wordStart = 0;
  const bool splittingNextWord = baselineBreakPos < line.size() && line[baselineBreakPos] == ' ';
  if (splittingNextWord) {
    wordStart = baselineBreakPos;
    while (wordStart < line.size() && line[wordStart] == ' ') ++wordStart;
  }
  if (wordStart >= line.size()) return false;

  size_t wordEnd = wordStart;
  while (wordEnd < line.size() && wordEnd - wordStart < MAX_HANGUL_WORD_BYTES &&
         !isAsciiWhitespace(static_cast<uint8_t>(line[wordEnd]))) {
    ++wordEnd;
  }
  if (wordEnd - wordStart == MAX_HANGUL_WORD_BYTES && wordEnd < line.size() &&
      !isAsciiWhitespace(static_cast<uint8_t>(line[wordEnd]))) {
    return false;
  }
  if (wordEnd == line.size() && !logicalLineComplete) return false;
  if (wordEnd == wordStart || !isValidUtf8Range(line, wordStart, wordEnd)) return false;

  bool containsHangul = false;
  size_t validationPosition = wordStart;
  while (validationPosition < wordEnd) {
    uint32_t codepoint = 0;
    size_t length = 0;
    if (!decodeUtf8(line, validationPosition, codepoint, length)) return false;
    containsHangul = containsHangul || utf8IsHangul(codepoint);
    validationPosition += length;
  }
  if (!containsHangul || measurePrefixWithTemporaryEnd(line, wordEnd, fontId, renderer) <= viewportWidth) return false;

  size_t position = wordStart;
  uint32_t previous = 0;
  size_t length = 0;
  if (!decodeUtf8(line, position, previous, length)) return false;
  position += length;

  bool found = false;
  int widestWidth = -1;
  size_t widestBreak = 0;
  while (position < wordEnd) {
    const size_t candidate = position;
    uint32_t current = 0;
    if (!decodeUtf8(line, position, current, length)) return false;
    position += length;

    if ((!splittingNextWord || candidate > baselineBreakPos) && candidate < wordEnd &&
        cjklinebreak::hangulBoundaryAllowsBreak(previous, current)) {
      const int width = measurePrefixWithTemporaryEnd(line, candidate, fontId, renderer);
      if (width <= viewportWidth && (!found || width > widestWidth)) {
        found = true;
        widestWidth = width;
        widestBreak = candidate;
      }
    }
    previous = current;
  }

  if (!found) return false;
  selectedBreakPos = widestBreak;
  return true;
}
}  // namespace

void TxtPageHistory::reset(const size_t startingOffset) {
  head = 0;
  count = 1;
  cursor = 0;
  firstPage = 0;
  offsets[0] = startingOffset;
}

bool TxtPageHistory::resetWithPrevious(const std::array<size_t, 3>& previousOffsets, const size_t previousCount,
                                       const size_t currentOffset) {
  if (previousCount > previousOffsets.size()) return false;
  size_t previous = 0;
  for (size_t i = 0; i < previousCount; ++i) {
    if (previousOffsets[i] >= currentOffset || (i > 0 && previousOffsets[i] <= previous)) return false;
    previous = previousOffsets[i];
  }

  head = 0;
  count = previousCount + 1;
  cursor = previousCount;
  firstPage = 0;
  for (size_t i = 0; i < previousCount; ++i) offsets[i] = previousOffsets[i];
  offsets[previousCount] = currentOffset;
  return true;
}

size_t TxtPageHistory::copyPreviousOffsets(std::array<size_t, 3>& previousOffsets) const {
  previousOffsets.fill(0);
  if (count == 0 || cursor == 0) return 0;

  const size_t previousCount = cursor < previousOffsets.size() ? cursor : previousOffsets.size();
  const size_t firstPrevious = cursor - previousCount;
  for (size_t i = 0; i < previousCount; ++i) {
    previousOffsets[i] = offsets[(head + firstPrevious + i) % CAPACITY];
  }
  return previousCount;
}

size_t TxtPageHistory::currentPage() const { return count == 0 ? 0 : firstPage + cursor; }

size_t TxtPageHistory::currentOffset() const { return count == 0 ? 0 : offsets[(head + cursor) % CAPACITY]; }

bool TxtPageHistory::hasForward() const { return count > 0 && cursor + 1 < count; }

bool TxtPageHistory::advance(const size_t nextOffset) {
  if (count == 0 || cursor + 1 != count || nextOffset <= currentOffset()) return false;

  if (count < CAPACITY) {
    offsets[(head + count) % CAPACITY] = nextOffset;
    ++count;
    ++cursor;
  } else {
    head = (head + 1) % CAPACITY;
    ++firstPage;
    offsets[(head + count - 1) % CAPACITY] = nextOffset;
  }
  return true;
}

bool TxtPageHistory::forward() {
  if (!hasForward()) return false;
  ++cursor;
  return true;
}

bool TxtPageHistory::retreat() {
  if (count == 0 || cursor == 0) return false;
  --cursor;
  return true;
}

bool TxtPageReader::loadPage(const Txt& txt, const GfxRenderer& renderer, const int fontId, const int viewportWidth,
                             const int linesPerPage, const size_t offset, std::vector<std::string>& outLines,
                             size_t& nextOffset, const bool hyphenationEnabled) {
  outLines.clear();
  nextOffset = offset;
  const size_t fileSize = txt.getFileSize();
  if (offset >= fileSize || linesPerPage <= 0) return false;

  const size_t requestedSize = std::min(READ_WINDOW_SIZE, fileSize - offset);
  if (sourceBufferCapacity < requestedSize + 1) {
    auto resizedBuffer = makeUniqueNoThrow<uint8_t[]>(requestedSize + 1);
    if (!resizedBuffer) return false;
    sourceBuffer = std::move(resizedBuffer);
    sourceBufferCapacity = requestedSize + 1;
  }

  uint8_t* const buffer = sourceBuffer.get();
  if (!txt.readContent(buffer, offset, requestedSize)) return false;
  buffer[requestedSize] = 0;

  if (renderer.isSdCardFont(fontId)) {
    renderer.ensureSdCardFontReady(fontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x01);
  }

  const size_t dataSize = completePrefixLength(buffer, requestedSize, offset + requestedSize >= fileSize);
  size_t pos = 0;
  if (offset > 0 && buffer[0] == '\n') {
    uint8_t previousByte = 0;
    if (!txt.readContent(&previousByte, offset - 1, 1)) return false;
    if (previousByte == '\r') pos = 1;
  }
  while (pos < dataSize && static_cast<int>(outLines.size()) < linesPerPage) {
    size_t lineEnd = pos;
    while (lineEnd < dataSize && buffer[lineEnd] != '\n') {
      ++lineEnd;
    }

    const bool hasNewline = lineEnd < dataSize;
    const bool lineComplete = hasNewline || offset + lineEnd >= fileSize;
    if (!lineComplete && !outLines.empty()) break;

    const size_t lineContentLength = lineEnd - pos;
    const bool hasCR = lineContentLength > 0 && buffer[pos + lineContentLength - 1] == '\r';
    const size_t displayLength = hasCR ? lineContentLength - 1 : lineContentLength;
    std::string line(reinterpret_cast<char*>(buffer + pos), displayLength);
    size_t lineBytePos = 0;

    do {
      if (line.empty()) {
        outLines.emplace_back();
        break;
      }

      const int lineWidth = renderer.getTextAdvanceX(fontId, line.c_str(), EpdFontFamily::REGULAR);
      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLength;
        line.clear();
        break;
      }

      size_t breakPos = line.length();
      while (breakPos > 0 && renderer.getTextAdvanceX(fontId, line.substr(0, breakPos).c_str(),
                                                      EpdFontFamily::REGULAR) > viewportWidth) {
        const size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          breakPos = previousUtf8Boundary(line, breakPos - 1);
        }
      }

      if (breakPos == 0) breakPos = firstUtf8UnitLength(line);
      bool hangulSplitSelected = false;
      if (hyphenationEnabled) {
        size_t hangulBreakPos = 0;
        if (selectHangulLineEndBreak(line, breakPos, lineComplete, viewportWidth, fontId, renderer, hangulBreakPos)) {
          breakPos = hangulBreakPos;
          hangulSplitSelected = true;
        }
      }
      if (!hangulSplitSelected) breakPos = adjustPunctuationBreak(line, breakPos, viewportWidth, fontId, renderer);
      outLines.push_back(line.substr(0, breakPos));

      size_t consumed = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') ++consumed;
      lineBytePos += consumed;
      line = line.substr(consumed);
    } while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage);

    if (line.empty()) {
      pos = hasNewline ? lineEnd + 1 : lineEnd;
    } else {
      pos += lineBytePos;
      break;
    }
  }

  size_t consumed = pos;
  if (consumed == requestedSize && requestedSize > 0 && buffer[requestedSize - 1] == '\r' &&
      offset + consumed < fileSize) {
    uint8_t nextByte = 0;
    if (!txt.readContent(&nextByte, offset + consumed, 1)) return false;
    if (nextByte == '\n') ++consumed;
  }

  nextOffset = std::min(offset + consumed, fileSize);
  return !outLines.empty() && nextOffset > offset;
}
