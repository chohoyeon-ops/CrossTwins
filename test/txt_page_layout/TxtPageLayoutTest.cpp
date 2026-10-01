#include <gtest/gtest.h>

#include <TxtPageLayout.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {
using txt::PageLayout;
using txt::PageLine;

struct PageResult {
  std::vector<PageLine> lines;
  size_t nextOffset = 0;
  bool loaded = false;
};

PageResult loadPage(const std::string& source, size_t offset, int maxLines, int viewportWidth) {
  const auto window = PageLayout::readWindow(offset, source.size());
  std::vector<uint8_t> bytes(window.length);
  std::copy_n(reinterpret_cast<const uint8_t*>(source.data() + window.offset), window.length, bytes.data());
  const bool startsParagraph = offset == 0 || bytes[window.pagePrefix - 1] == '\n';
  PageResult result;
  auto width = [](const char* text) {
    // Each UTF-8 codepoint has unit width; continuation bytes add no width.
    int count = 0;
    for (const auto* p = reinterpret_cast<const uint8_t*>(text); *p; ++p) {
      if ((*p & 0xC0) != 0x80) ++count;
    }
    return count;
  };
  result.loaded = PageLayout::loadPage(bytes.data(), window.offset, window.length, offset, source.size(), maxLines,
                                       viewportWidth, startsParagraph, width, result.lines, result.nextOffset);
  return result;
}

bool hasOnlyCompleteUtf8(const std::string& text) {
  size_t pos = 0;
  while (pos < text.size()) {
    const auto lead = static_cast<uint8_t>(text[pos]);
    size_t length = 1;
    if ((lead & 0xE0) == 0xC0) length = 2;
    else if ((lead & 0xF0) == 0xE0) length = 3;
    else if ((lead & 0xF8) == 0xF0) length = 4;
    if (pos + length > text.size()) return false;
    for (size_t i = 1; i < length; ++i) {
      if ((static_cast<uint8_t>(text[pos + i]) & 0xC0) != 0x80) return false;
    }
    pos += length;
  }
  return true;
}
}  // namespace

TEST(TxtPageLayout, KeepsMixedUtf8CodepointsTogether) {
  const std::string source = "A\xC3\xA9\xED\x95\x9C\xE6\xB0\xB4\xF0\x9F\x98\x80Z";
  const auto page = loadPage(source, 0, 4, 20);

  ASSERT_TRUE(page.loaded);
  ASSERT_EQ(page.lines.size(), 1u);
  EXPECT_EQ(page.lines[0].text, source);
  EXPECT_TRUE(hasOnlyCompleteUtf8(page.lines[0].text));
  EXPECT_EQ(page.lines[0].sourceStart, 0u);
  EXPECT_EQ(page.lines[0].sourceEnd, source.size());
  EXPECT_TRUE(page.lines[0].paragraphStart);
  EXPECT_TRUE(page.lines[0].paragraphEnd);
  EXPECT_EQ(page.nextOffset, source.size());
}

TEST(TxtPageLayout, CompletesFourByteCodepointAcrossEightKiBReadBoundary) {
  const std::string emoji = "\xF0\x9F\x98\x80";
  const std::string source = std::string(PageLayout::CHUNK_SIZE - 1, 'a') + emoji + "z";
  const auto first = loadPage(source, 0, 1, static_cast<int>(PageLayout::CHUNK_SIZE + 1));

  ASSERT_TRUE(first.loaded);
  ASSERT_EQ(first.lines.size(), 1u);
  EXPECT_EQ(first.lines[0].text.size(), PageLayout::CHUNK_SIZE - 1 + emoji.size());
  EXPECT_TRUE(first.lines[0].text.ends_with(emoji));
  EXPECT_TRUE(hasOnlyCompleteUtf8(first.lines[0].text));
  EXPECT_EQ(first.nextOffset, PageLayout::CHUNK_SIZE - 1 + emoji.size());

  const auto second = loadPage(source, first.nextOffset, 1, 4);
  ASSERT_TRUE(second.loaded);
  ASSERT_EQ(second.lines.size(), 1u);
  EXPECT_EQ(second.lines[0].text, "z");
  EXPECT_EQ(second.lines[0].sourceStart, first.nextOffset);
  EXPECT_EQ(second.nextOffset, source.size());
}

TEST(TxtPageLayout, DefersLeadByteNearReadWindowEndUntilComplete) {
  const std::string cjk = "\xE6\xB0\xB4";
  const std::string source = std::string(PageLayout::CHUNK_SIZE + 2, 'a') + cjk + "z";
  const auto first = loadPage(source, 0, 1, static_cast<int>(PageLayout::CHUNK_SIZE + 1));

  ASSERT_TRUE(first.loaded);
  EXPECT_EQ(first.nextOffset, PageLayout::CHUNK_SIZE);
  EXPECT_EQ(first.lines[0].text.size(), PageLayout::CHUNK_SIZE);
  EXPECT_TRUE(hasOnlyCompleteUtf8(first.lines[0].text));

  const auto second = loadPage(source, first.nextOffset, 1, 10);
  ASSERT_TRUE(second.loaded);
  ASSERT_EQ(second.lines.size(), 1u);
  EXPECT_EQ(second.lines[0].text, std::string("aa") + cjk + "z");
  EXPECT_EQ(second.nextOffset, source.size());
}

TEST(TxtPageLayout, IncompleteReadTailReturnsPriorTextWithoutRetryingCursor) {
  const uint8_t partial[] = {'A', 0xE6, 0xB0};
  std::vector<PageLine> lines;
  size_t nextOffset = 0;
  auto width = [](const char* text) { return static_cast<int>(std::char_traits<char>::length(text)); };

  ASSERT_TRUE(PageLayout::loadPage(partial, 0, sizeof(partial), 0, sizeof(partial) + 1, 4, 20, true, width, lines,
                                   nextOffset));
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].text, "A");
  EXPECT_EQ(nextOffset, 1u);
}

TEST(TxtPageLayout, MalformedLeadBeforeLfDoesNotBlockFollowingChunks) {
  const std::string malformedLead(1, static_cast<char>(0xE6));
  const std::string source = "A" + malformedLead + "\n" + std::string(PageLayout::CHUNK_SIZE + 900, 'x');
  size_t offset = 0;
  size_t previousEnd = 0;
  bool firstPage = true;

  while (offset < source.size()) {
    const auto page = loadPage(source, offset, 1, static_cast<int>(PageLayout::CHUNK_SIZE + 1));
    ASSERT_TRUE(page.loaded);
    ASSERT_GT(page.nextOffset, offset);
    ASSERT_LE(page.nextOffset, source.size());
    for (const auto& line : page.lines) {
      EXPECT_EQ(line.sourceStart, previousEnd);
      EXPECT_GT(line.sourceEnd, line.sourceStart);
      previousEnd = line.sourceEnd;
    }
    EXPECT_EQ(page.nextOffset, previousEnd);

    if (firstPage) {
      ASSERT_EQ(page.lines.size(), 1u);
      EXPECT_EQ(page.lines[0].text.size(), 2u);
      EXPECT_EQ(page.lines[0].text[1], static_cast<char>(0xE6));
      EXPECT_TRUE(page.lines[0].paragraphEnd);
      EXPECT_EQ(page.nextOffset, 3u);
      firstPage = false;
    } else if (offset == 3) {
      EXPECT_TRUE(page.lines[0].paragraphStart);
    }
    offset = page.nextOffset;
  }

  EXPECT_FALSE(firstPage);
  EXPECT_EQ(offset, source.size());
  EXPECT_EQ(previousEnd, source.size());
}

TEST(TxtPageLayout, TracksLfCrLfBlankLinesAndEofRanges) {
  const std::string source = "abc\n\r\nx\r\ny";
  const auto page = loadPage(source, 0, 8, 20);

  ASSERT_TRUE(page.loaded);
  ASSERT_EQ(page.lines.size(), 4u);
  EXPECT_EQ(page.lines[0].text, "abc");
  EXPECT_EQ(page.lines[0].sourceStart, 0u);
  EXPECT_EQ(page.lines[0].sourceEnd, 4u);
  EXPECT_TRUE(page.lines[0].paragraphStart);
  EXPECT_TRUE(page.lines[0].paragraphEnd);
  EXPECT_TRUE(page.lines[1].text.empty());
  EXPECT_EQ(page.lines[1].sourceStart, 4u);
  EXPECT_EQ(page.lines[1].sourceEnd, 6u);
  EXPECT_TRUE(page.lines[1].paragraphStart);
  EXPECT_TRUE(page.lines[1].paragraphEnd);
  EXPECT_EQ(page.lines[2].text, "x");
  EXPECT_EQ(page.lines[2].sourceStart, 6u);
  EXPECT_EQ(page.lines[2].sourceEnd, 9u);
  EXPECT_EQ(page.lines[3].text, "y");
  EXPECT_EQ(page.lines[3].sourceStart, 9u);
  EXPECT_EQ(page.lines[3].sourceEnd, source.size());
  EXPECT_TRUE(page.lines[3].paragraphEnd);
  EXPECT_EQ(page.nextOffset, source.size());
}

TEST(TxtPageLayout, MarksParagraphContinuationAcrossPages) {
  const std::string source = "abcdef\n";
  const auto first = loadPage(source, 0, 1, 2);
  ASSERT_TRUE(first.loaded);
  EXPECT_EQ(first.lines[0].text, "ab");
  EXPECT_TRUE(first.lines[0].paragraphStart);
  EXPECT_FALSE(first.lines[0].paragraphEnd);

  const auto second = loadPage(source, first.nextOffset, 1, 2);
  ASSERT_TRUE(second.loaded);
  EXPECT_EQ(second.lines[0].text, "cd");
  EXPECT_FALSE(second.lines[0].paragraphStart);
  EXPECT_FALSE(second.lines[0].paragraphEnd);

  const auto third = loadPage(source, second.nextOffset, 1, 2);
  ASSERT_TRUE(third.loaded);
  EXPECT_EQ(third.lines[0].text, "ef");
  EXPECT_FALSE(third.lines[0].paragraphStart);
  EXPECT_TRUE(third.lines[0].paragraphEnd);
  EXPECT_EQ(third.lines[0].sourceEnd, source.size());
}

TEST(TxtPageLayout, ConsumesWrapSpaceExactlyAndKeepsRangesContiguous) {
  const std::string source = "ab cd";
  const auto first = loadPage(source, 0, 1, 2);
  ASSERT_TRUE(first.loaded);
  ASSERT_EQ(first.lines.size(), 1u);
  EXPECT_EQ(first.lines[0].text, "ab");
  EXPECT_EQ(first.lines[0].sourceStart, 0u);
  EXPECT_EQ(first.lines[0].sourceEnd, 3u);
  EXPECT_EQ(first.nextOffset, 3u);

  const auto second = loadPage(source, first.nextOffset, 1, 2);
  ASSERT_TRUE(second.loaded);
  ASSERT_EQ(second.lines.size(), 1u);
  EXPECT_EQ(second.lines[0].text, "cd");
  EXPECT_EQ(second.lines[0].sourceStart, first.nextOffset);
  EXPECT_EQ(second.lines[0].sourceEnd, source.size());
  EXPECT_EQ(second.nextOffset, source.size());
}

TEST(TxtPageLayout, WideGlyphConsumesOneCompleteCodepoint) {
  const std::string emoji = "\xF0\x9F\x98\x80";
  const std::string source = emoji + "x";
  const auto first = loadPage(source, 0, 1, 0);

  ASSERT_TRUE(first.loaded);
  ASSERT_EQ(first.lines.size(), 1u);
  EXPECT_EQ(first.lines[0].text, emoji);
  EXPECT_TRUE(hasOnlyCompleteUtf8(first.lines[0].text));
  EXPECT_EQ(first.nextOffset, emoji.size());
}

TEST(TxtPageLayout, LongUnbrokenParagraphMakesMonotonicExactProgress) {
  const std::string source(20000, 'a');
  size_t offset = 0;
  std::string reconstructed;
  size_t previousEnd = 0;

  while (offset < source.size()) {
    const auto page = loadPage(source, offset, 8, 80);
    ASSERT_TRUE(page.loaded);
    ASSERT_GT(page.nextOffset, offset);
    ASSERT_LE(page.nextOffset, source.size());
    for (const auto& line : page.lines) {
      EXPECT_EQ(line.sourceStart, previousEnd);
      EXPECT_LE(line.sourceStart, line.sourceEnd);
      reconstructed += line.text;
      previousEnd = line.sourceEnd;
    }
    EXPECT_EQ(page.nextOffset, previousEnd);
    offset = page.nextOffset;
  }

  EXPECT_EQ(offset, source.size());
  EXPECT_EQ(previousEnd, source.size());
  EXPECT_EQ(reconstructed, source);
}

TEST(TxtPageLayout, EmptyInputDoesNotProduceAProgressingPage) {
  const auto page = loadPage("", 0, 4, 20);
  EXPECT_FALSE(page.loaded);
  EXPECT_TRUE(page.lines.empty());
  EXPECT_EQ(page.nextOffset, 0u);
}
