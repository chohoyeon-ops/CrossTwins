#include <gtest/gtest.h>

#include <BidiUtils.h>
#include <TxtPageLayout.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {
using txt::PageLayout;
using txt::PageLine;

bool startsWithRtlForTest(const char* text) {
  return BidiUtils::startsWithRtl(text, BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
}

int renderedIndentFor(const PageLine& line, int indentWidth, PageLayout::Geometry::Alignment alignment) {
  return PageLayout::indentForLine(line, indentWidth, alignment, startsWithRtlForTest(line.text.c_str()));
}

struct PageResult {
  std::vector<PageLine> lines;
  size_t nextOffset = 0;
  bool loaded = false;
};

template <typename TextAdvance>
PageResult loadPageWithAdvance(const std::string& source, size_t offset, int maxLines, int viewportWidth,
                               const PageLayout::Geometry& geometry, TextAdvance textAdvance) {
  const auto window = PageLayout::readWindow(offset, source.size());
  std::vector<uint8_t> bytes(window.length);
  std::copy_n(reinterpret_cast<const uint8_t*>(source.data() + window.offset), window.length, bytes.data());
  const bool startsParagraph = offset == 0 || bytes[window.pagePrefix - 1] == '\n';
  PageResult result;
  result.loaded = PageLayout::loadPage(bytes.data(), window.offset, window.length, offset, source.size(), maxLines,
                                       viewportWidth, startsParagraph, textAdvance, result.lines,
                                       result.nextOffset, geometry);
  return result;
}

PageResult loadPage(const std::string& source, size_t offset, int maxLines, int viewportWidth,
                    const PageLayout::Geometry& geometry = {}) {
  auto width = [](const char* text) {
    // Each UTF-8 codepoint has unit width; continuation bytes add no width.
    int count = 0;
    for (const auto* p = reinterpret_cast<const uint8_t*>(text); *p; ++p) {
      if ((*p & 0xC0) != 0x80) ++count;
    }
    return count;
  };
  return loadPageWithAdvance(source, offset, maxLines, viewportWidth, geometry, width);
}

int measureWithTracking(const char* text, int tracking) {
  int width = 0;
  uint32_t previous = 0;
  const auto* cursor = reinterpret_cast<const uint8_t*>(text);
  while (const uint32_t codepoint = utf8NextCodepoint(&cursor)) {
    if (utf8IsCombiningMark(codepoint) || BidiUtils::isTransparentMark(codepoint)) continue;
    const bool previousSpace = previous == ' ' || previous == 0xA0 || previous == 0x3000;
    const bool currentSpace = codepoint == ' ' || codepoint == 0xA0 || codepoint == 0x3000;
    if (previous != 0 && !previousSpace && !currentSpace) width += tracking;
    ++width;
    previous = codepoint;
  }
  return width;
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

struct FakeBreaks {
  std::vector<uint8_t> offsets;
  size_t calls = 0;
  txt::PageLayout::HyphenationStatus status = txt::PageLayout::HyphenationStatus::Candidates;
};

txt::PageLayout::HyphenationStatus findFakeBreaks(void* context, const char*, size_t, uint8_t* output,
                                                   size_t capacity, size_t& count) {
  auto& fake = *static_cast<FakeBreaks*>(context);
  ++fake.calls;
  count = 0;
  if (fake.status != txt::PageLayout::HyphenationStatus::Candidates) return fake.status;
  if (fake.offsets.size() > capacity) return txt::PageLayout::HyphenationStatus::Failure;
  for (const auto offset : fake.offsets) output[count++] = offset;
  return count == 0 ? txt::PageLayout::HyphenationStatus::NoCandidates : fake.status;
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

TEST(TxtPageLayout, FitsAsciiKoreanAndCjkPrefixesOnCompleteCodepoints) {
  const std::string source = "A\xED\x95\x9C\xE6\xB0\xB4" "B";
  const auto first = loadPage(source, 0, 1, 3);

  ASSERT_TRUE(first.loaded);
  ASSERT_EQ(first.lines.size(), 1u);
  EXPECT_EQ(first.lines[0].text, "A\xED\x95\x9C\xE6\xB0\xB4");
  EXPECT_TRUE(hasOnlyCompleteUtf8(first.lines[0].text));
  EXPECT_EQ(first.lines[0].sourceEnd, 7u);
  EXPECT_EQ(first.nextOffset, 7u);

  const auto second = loadPage(source, first.nextOffset, 1, 1);
  ASSERT_TRUE(second.loaded);
  EXPECT_EQ(second.lines[0].text, "B");
  EXPECT_EQ(second.lines[0].sourceStart, first.nextOffset);
  EXPECT_EQ(second.nextOffset, source.size());
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

TEST(TxtPageLayout, CharacterWrapKeepsAnOverflowingSpaceForTheNextLine) {
  auto geometry = PageLayout::Geometry{};
  geometry.wrapPolicy = PageLayout::Geometry::WrapPolicy::CHARACTER;
  const std::string source = "ab cd";
  const auto first = loadPage(source, 0, 1, 2, geometry);

  ASSERT_TRUE(first.loaded);
  ASSERT_EQ(first.lines.size(), 1u);
  EXPECT_EQ(first.lines[0].text, "ab");
  EXPECT_EQ(first.lines[0].sourceEnd, 2u);
  EXPECT_EQ(first.nextOffset, 2u);

  const auto second = loadPage(source, first.nextOffset, 1, 2, geometry);
  ASSERT_TRUE(second.loaded);
  EXPECT_EQ(second.lines[0].text, " c");
  EXPECT_EQ(second.lines[0].sourceStart, first.nextOffset);
  EXPECT_EQ(second.nextOffset, 4u);

  const auto whitespaceFirst = loadPage(source, 0, 1, 2);
  ASSERT_TRUE(whitespaceFirst.loaded);
  EXPECT_EQ(whitespaceFirst.lines[0].sourceEnd, 3u);
  EXPECT_EQ(whitespaceFirst.nextOffset, 3u);
}

TEST(TxtPageLayout, ExactFitLeavesTheNextWholeCodepointForTheFollowingPage) {
  const std::string source = "abcde";
  const auto first = loadPage(source, 0, 1, 3);

  ASSERT_TRUE(first.loaded);
  EXPECT_EQ(first.lines[0].text, "abc");
  EXPECT_EQ(first.lines[0].sourceEnd, 3u);
  EXPECT_EQ(first.nextOffset, 3u);

  const auto second = loadPage(source, first.nextOffset, 1, 3);
  ASSERT_TRUE(second.loaded);
  EXPECT_EQ(second.lines[0].text, "de");
  EXPECT_EQ(second.lines[0].sourceStart, 3u);
  EXPECT_EQ(second.nextOffset, source.size());
}

TEST(TxtPageLayout, CorrectsAContextualWidthChangeNearTheBinarySearchBoundary) {
  const std::string source = "abcx";
  const auto page = loadPageWithAdvance(
      source, 0, 1, 6, PageLayout::Geometry{}, [](const char* text) {
        const std::string prefix(text);
        if (prefix == "ab") return 10;
        if (prefix == "abc") return 5;
        if (prefix == "abcx") return 10;
        return static_cast<int>(prefix.size());
      });

  ASSERT_TRUE(page.loaded);
  EXPECT_EQ(page.lines[0].text, "abc");
  EXPECT_EQ(page.nextOffset, 3u);
}

TEST(TxtPageLayout, ChoosesWidestFittingHyphenCandidateAndPartitionsSourceBytes) {
  const std::string source = "abcdefghij";
  FakeBreaks fake{{3, 5, 7}};
  PageLayout::Hyphenation hyphenation{&fake, findFakeBreaks};
  auto width = [](const char* text) { return static_cast<int>(std::char_traits<char>::length(text)); };
  const auto window = PageLayout::readWindow(0, source.size());
  std::vector<PageLine> lines;
  size_t next = 0;

  ASSERT_TRUE(PageLayout::loadPage(reinterpret_cast<const uint8_t*>(source.data()), window.offset, window.length, 0,
                                   source.size(), 1, 6, true, width, lines, next, PageLayout::Geometry{}, hyphenation));
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ("abcde-", lines[0].text);
  EXPECT_EQ(0u, lines[0].sourceStart);
  EXPECT_EQ(5u, lines[0].sourceEnd);
  EXPECT_FALSE(lines[0].paragraphEnd);
  EXPECT_EQ(5u, next);
  EXPECT_EQ(1u, fake.calls);

  std::vector<PageLine> remainder;
  size_t remainderNext = 0;
  const auto nextWindow = PageLayout::readWindow(next, source.size());
  ASSERT_TRUE(PageLayout::loadPage(reinterpret_cast<const uint8_t*>(source.data() + nextWindow.offset),
                                   nextWindow.offset, nextWindow.length, next, source.size(), 1, 10, false, width,
                                   remainder, remainderNext, PageLayout::Geometry{}));
  ASSERT_EQ(1u, remainder.size());
  EXPECT_EQ("fghij", remainder[0].text);
  EXPECT_EQ(5u, remainder[0].sourceStart);
  EXPECT_EQ(source.size(), remainder[0].sourceEnd);
  EXPECT_EQ(source.size(), remainderNext);
}

TEST(TxtPageLayout, DisabledAndNoPatternHyphenationKeepExistingWrapExactly) {
  const std::string source = "abcdefghij";
  auto width = [](const char* text) { return static_cast<int>(std::char_traits<char>::length(text)); };
  const auto baseline = loadPageWithAdvance(source, 0, 1, 6, PageLayout::Geometry{}, width);
  FakeBreaks noPatterns{{}, 0, PageLayout::HyphenationStatus::NoCandidates};
  const auto window = PageLayout::readWindow(0, source.size());
  std::vector<PageLine> lines;
  size_t next = 0;
  ASSERT_TRUE(PageLayout::loadPage(reinterpret_cast<const uint8_t*>(source.data()), window.offset, window.length, 0,
                                   source.size(), 1, 6, true, width, lines, next, PageLayout::Geometry{},
                                   PageLayout::Hyphenation{&noPatterns, findFakeBreaks}));
  ASSERT_EQ(1u, lines.size());
  ASSERT_EQ(1u, baseline.lines.size());
  EXPECT_EQ(baseline.lines[0].text, lines[0].text);
  EXPECT_EQ(baseline.lines[0].sourceStart, lines[0].sourceStart);
  EXPECT_EQ(baseline.lines[0].sourceEnd, lines[0].sourceEnd);
  EXPECT_EQ(baseline.nextOffset, next);
  EXPECT_EQ(1u, noPatterns.calls);
}

TEST(TxtPageLayout, NonFittingPatternCandidatesFallBackToTheOriginalUnhyphenatedLine) {
  const std::string source = "abcdefghij";
  FakeBreaks fake{{3, 5, 7}};
  const auto window = PageLayout::readWindow(0, source.size());
  std::vector<PageLine> lines;
  size_t next = 0;
  auto width = [](const char* text) { return static_cast<int>(std::char_traits<char>::length(text)); };

  ASSERT_TRUE(PageLayout::loadPage(reinterpret_cast<const uint8_t*>(source.data()), window.offset, window.length, 0,
                                   source.size(), 1, 3, true, width, lines, next, PageLayout::Geometry{},
                                   PageLayout::Hyphenation{&fake, findFakeBreaks}));
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ("abc", lines[0].text);
  EXPECT_EQ(3u, lines[0].sourceEnd);
  EXPECT_EQ(3u, next);
}

TEST(TxtPageLayout, HyphenationFailureLeavesNoPartialPageOrConsumedBytes) {
  const std::string source = "abcdefghij";
  FakeBreaks failure{{}, 0, PageLayout::HyphenationStatus::Failure};
  const auto window = PageLayout::readWindow(0, source.size());
  std::vector<PageLine> lines;
  size_t next = 99;
  auto width = [](const char* text) { return static_cast<int>(std::char_traits<char>::length(text)); };

  EXPECT_FALSE(PageLayout::loadPage(reinterpret_cast<const uint8_t*>(source.data()), window.offset, window.length, 0,
                                    source.size(), 1, 6, true, width, lines, next, PageLayout::Geometry{},
                                    PageLayout::Hyphenation{&failure, findFakeBreaks}));
  EXPECT_TRUE(lines.empty());
  EXPECT_EQ(0u, next);
}

TEST(TxtPageLayout, HyphenationIgnoresOverlongAndMixedOrCompoundAsciiRuns) {
  const std::vector<std::string> sources{std::string(65, 'a'), "co-opportunity", "word'suffix"};
  for (const std::string& source : sources) {
    FakeBreaks fake{{3, 5}};
    const auto page = loadPageWithAdvance(source, 0, 1, 4, PageLayout::Geometry{},
                                          [](const char* text) { return static_cast<int>(std::char_traits<char>::length(text)); });
    const auto window = PageLayout::readWindow(0, source.size());
    std::vector<PageLine> lines;
    size_t next = 0;
    auto width = [](const char* text) { return static_cast<int>(std::char_traits<char>::length(text)); };
    ASSERT_TRUE(PageLayout::loadPage(reinterpret_cast<const uint8_t*>(source.data()), window.offset, window.length, 0,
                                     source.size(), 1, 4, true, width, lines, next, PageLayout::Geometry{},
                                     PageLayout::Hyphenation{&fake, findFakeBreaks}));
    ASSERT_EQ(1u, page.lines.size());
    EXPECT_EQ(page.lines[0].text, lines[0].text);
    EXPECT_EQ(page.nextOffset, next);
    EXPECT_EQ(0u, fake.calls);
  }
}

TEST(TxtPageLayout, MeasuresTheAvailableTailWhenAnExponentialProbeReachesEof) {
  for (const std::string source : {"abcdef", "abcdefghi"}) {
    const auto page = loadPage(source, 0, 1, 20);

    ASSERT_TRUE(page.loaded);
    ASSERT_EQ(page.lines.size(), 1u);
    EXPECT_EQ(page.lines[0].text, source);
    EXPECT_EQ(page.lines[0].sourceEnd, source.size());
    EXPECT_EQ(page.nextOffset, source.size());
  }
}

TEST(TxtPageLayout, ContinuesPastSpeculativeChunkBoundaryWithoutLosingSourceBytes) {
  const std::string source(9000, 'a');
  const auto unitWidth = [](const char* text) { return static_cast<int>(std::char_traits<char>::length(text)); };
  const PageLayout::Geometry geometry{300, 1, 0, 0};
  const auto first = loadPageWithAdvance(source, 0, 300, 33, geometry, unitWidth);

  ASSERT_TRUE(first.loaded);
  EXPECT_EQ(first.nextOffset, PageLayout::CHUNK_SIZE);

  std::string reconstructed;
  size_t consumed = 0;
  for (const auto& line : first.lines) {
    EXPECT_EQ(line.sourceStart, consumed);
    ASSERT_GE(line.sourceEnd, line.sourceStart);
    reconstructed.append(source, line.sourceStart, line.sourceEnd - line.sourceStart);
    consumed = line.sourceEnd;
  }
  EXPECT_EQ(consumed, first.nextOffset);
  EXPECT_EQ(reconstructed, source.substr(0, first.nextOffset));

  const auto second = loadPageWithAdvance(source, first.nextOffset, 300, 33, geometry, unitWidth);
  ASSERT_TRUE(second.loaded);
  EXPECT_EQ(second.lines.front().sourceStart, first.nextOffset);
  EXPECT_EQ(second.nextOffset, source.size());
  for (const auto& line : second.lines) {
    EXPECT_EQ(line.sourceStart, consumed);
    reconstructed.append(source, line.sourceStart, line.sourceEnd - line.sourceStart);
    consumed = line.sourceEnd;
  }
  EXPECT_EQ(consumed, source.size());
  EXPECT_EQ(reconstructed, source);
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

TEST(TxtPageLayout, UsesOnePositiveLineAdvanceForPagination) {
  EXPECT_EQ(PageLayout::effectiveLineAdvance(20), 20);
  EXPECT_EQ(PageLayout::calculateLinesPerPage(480, 20), 24);
  EXPECT_EQ(PageLayout::effectiveLineAdvance(0), 1);
  EXPECT_EQ(PageLayout::calculateLinesPerPage(480, 0), 480);
  EXPECT_EQ(PageLayout::calculateLinesPerPage(0, 20), 1);
}

TEST(TxtPageLayout, MapsSourceAnchorsToGreatestPageStartAtOrBeforeAnchor) {
  const std::vector<size_t> pageOffsets{0, 100, 200, 300};
  EXPECT_EQ(PageLayout::pageForSourceAnchor(pageOffsets, 0), 0u);
  EXPECT_EQ(PageLayout::pageForSourceAnchor(pageOffsets, 99), 0u);
  EXPECT_EQ(PageLayout::pageForSourceAnchor(pageOffsets, 100), 1u);
  EXPECT_EQ(PageLayout::pageForSourceAnchor(pageOffsets, 199), 1u);
  EXPECT_EQ(PageLayout::pageForSourceAnchor(pageOffsets, 200), 2u);
  EXPECT_EQ(PageLayout::pageForSourceAnchor(pageOffsets, 201), 2u);
  EXPECT_EQ(PageLayout::pageForSourceAnchor(pageOffsets, 300), 3u);
  EXPECT_EQ(PageLayout::pageForSourceAnchor(pageOffsets, 500), 3u);
}

TEST(TxtPageLayout, DefersParagraphStartWhenGapCannotFitAndOmitsGapOnNewPage) {
  const std::string source = "first\nsecond";
  const PageLayout::Geometry geometry{24, 10, 5, 0};
  const auto first = loadPage(source, 0, 2, 30, geometry);

  ASSERT_TRUE(first.loaded);
  ASSERT_EQ(first.lines.size(), 1u);
  EXPECT_EQ(first.lines[0].text, "first");
  EXPECT_TRUE(first.lines[0].paragraphEnd);
  EXPECT_EQ(first.nextOffset, 6u);
  EXPECT_EQ(PageLayout::gapBeforeLine(first.lines[0], true, geometry.paragraphGap), 0);

  const auto second = loadPage(source, first.nextOffset, 2, 30, geometry);
  ASSERT_TRUE(second.loaded);
  ASSERT_EQ(second.lines.size(), 1u);
  EXPECT_EQ(second.lines[0].text, "second");
  EXPECT_TRUE(second.lines[0].paragraphStart);
  EXPECT_EQ(PageLayout::gapBeforeLine(second.lines[0], true, geometry.paragraphGap), 0);
  EXPECT_EQ(second.nextOffset, source.size());
}

TEST(TxtPageLayout, AppliesIndentOnlyToFirstWrappedSegmentAndKeepsContinuationUnindented) {
  const std::string source = "abcdefghijklmnopqrst";
  const int indentWidth = PageLayout::indentWidthForSpaces(2, 2);
  EXPECT_EQ(indentWidth, 4);
  const PageLayout::Geometry geometry{20, 10, 5, indentWidth};

  const auto first = loadPage(source, 0, 2, 8, geometry);
  ASSERT_TRUE(first.loaded);
  ASSERT_EQ(first.lines.size(), 2u);
  EXPECT_EQ(first.lines[0].text, "abcd");
  EXPECT_TRUE(first.lines[0].paragraphStart);
  EXPECT_EQ(PageLayout::indentForLine(first.lines[0], indentWidth, PageLayout::Geometry::Alignment::JUSTIFIED, false),
            4);
  EXPECT_EQ(first.lines[1].text, "efghijkl");
  EXPECT_FALSE(first.lines[1].paragraphStart);
  EXPECT_EQ(PageLayout::indentForLine(first.lines[1], indentWidth, PageLayout::Geometry::Alignment::JUSTIFIED,
                                      false),
            0);
  EXPECT_EQ(first.nextOffset, 12u);

  const auto second = loadPage(source, first.nextOffset, 2, 8, geometry);
  ASSERT_TRUE(second.loaded);
  ASSERT_EQ(second.lines.size(), 1u);
  EXPECT_EQ(second.lines[0].text, "mnopqrst");
  EXPECT_FALSE(second.lines[0].paragraphStart);
  EXPECT_EQ(PageLayout::gapBeforeLine(second.lines[0], true, geometry.paragraphGap), 0);
  EXPECT_EQ(second.nextOffset, source.size());
}

TEST(TxtPageLayout, PreservesConsecutiveBlankParagraphsAndFitsTheFinalLineAtBoundary) {
  const std::string source = "a\n\n\nb";
  const PageLayout::Geometry geometry{55, 10, 5, 0};
  const auto page = loadPage(source, 0, 5, 20, geometry);

  ASSERT_TRUE(page.loaded);
  ASSERT_EQ(page.lines.size(), 4u);
  EXPECT_EQ(page.lines[0].text, "a");
  EXPECT_TRUE(page.lines[1].text.empty());
  EXPECT_TRUE(page.lines[2].text.empty());
  EXPECT_EQ(page.lines[3].text, "b");
  int usedHeight = 0;
  for (size_t i = 0; i < page.lines.size(); ++i) {
    usedHeight += PageLayout::gapBeforeLine(page.lines[i], i == 0, geometry.paragraphGap);
    usedHeight += geometry.lineAdvance;
  }
  EXPECT_EQ(usedHeight, geometry.viewportHeight);
  EXPECT_EQ(page.lines.back().sourceEnd, source.size());
  EXPECT_EQ(page.nextOffset, source.size());
}

TEST(TxtPageLayout, ExactGapAndLineHeightFitAtThePageBoundary) {
  const std::string source = "a\nb";
  const PageLayout::Geometry geometry{25, 10, 5, 0};
  const auto page = loadPage(source, 0, 2, 10, geometry);

  ASSERT_TRUE(page.loaded);
  ASSERT_EQ(page.lines.size(), 2u);
  EXPECT_EQ(page.lines[0].text, "a");
  EXPECT_EQ(page.lines[1].text, "b");
  EXPECT_EQ(PageLayout::gapBeforeLine(page.lines[1], false, geometry.paragraphGap), 5);
  EXPECT_EQ(page.nextOffset, source.size());
}

TEST(TxtPageLayout, CenterAndLtrRightKeepFullFirstLineWidthWithoutIndent) {
  const std::string source = "abcdefghijkl";
  const int indentWidth = PageLayout::indentWidthForSpaces(2, 2);
  const auto alignment = PageLayout::Geometry::Alignment::CENTER;
  const PageLayout::Geometry centered{20, 10, 0, indentWidth, alignment, startsWithRtlForTest};
  const auto centerPage = loadPage(source, 0, 1, 8, centered);

  ASSERT_TRUE(centerPage.loaded);
  ASSERT_EQ(centerPage.lines.size(), 1u);
  EXPECT_EQ(centerPage.lines[0].text, "abcdefgh");
  EXPECT_EQ(centerPage.nextOffset, 8u);
  EXPECT_EQ(renderedIndentFor(centerPage.lines[0], indentWidth, alignment), 0);

  const PageLayout::Geometry rightAligned{20, 10, 0, indentWidth, PageLayout::Geometry::Alignment::RIGHT,
                                          startsWithRtlForTest};
  const auto rightPage = loadPage(source, 0, 1, 8, rightAligned);

  ASSERT_TRUE(rightPage.loaded);
  ASSERT_EQ(rightPage.lines.size(), 1u);
  EXPECT_EQ(rightPage.lines[0].text, "abcdefgh");
  EXPECT_EQ(rightPage.nextOffset, 8u);
  EXPECT_EQ(renderedIndentFor(rightPage.lines[0], indentWidth, PageLayout::Geometry::Alignment::RIGHT), 0);
}

TEST(TxtPageLayout, NaturalAlignmentUsesTheConfiguredFirstLineIndent) {
  const std::string source = "abcdefghijkl";
  const int indentWidth = PageLayout::indentWidthForSpaces(2, 2);
  const PageLayout::Geometry leftAligned{20, 10, 0, indentWidth, PageLayout::Geometry::Alignment::LEFT,
                                         startsWithRtlForTest};
  const auto page = loadPage(source, 0, 1, 8, leftAligned);

  ASSERT_TRUE(page.loaded);
  ASSERT_EQ(page.lines.size(), 1u);
  EXPECT_EQ(page.lines[0].text, "abcd");
  EXPECT_EQ(page.nextOffset, 4u);
  EXPECT_EQ(renderedIndentFor(page.lines[0], indentWidth, PageLayout::Geometry::Alignment::LEFT), 4);
  EXPECT_TRUE(PageLayout::isNaturalAlignment(PageLayout::Geometry::Alignment::JUSTIFIED, false));
  EXPECT_TRUE(PageLayout::isNaturalAlignment(PageLayout::Geometry::Alignment::LEFT, true));
}

TEST(TxtPageLayout, NaturalRtlRightAlignmentUsesTheSameIndentAndWrapWidth) {
  const std::string source = "\xD7\x90" "abcdefghij";
  const int indentWidth = PageLayout::indentWidthForSpaces(2, 2);
  const PageLayout::Geometry rightAligned{20, 10, 0, indentWidth, PageLayout::Geometry::Alignment::RIGHT,
                                          startsWithRtlForTest};
  const auto page = loadPage(source, 0, 1, 8, rightAligned);

  ASSERT_TRUE(page.loaded);
  ASSERT_EQ(page.lines.size(), 1u);
  EXPECT_EQ(page.lines[0].text, "\xD7\x90" "abc");
  EXPECT_EQ(page.nextOffset, 5u);
  EXPECT_EQ(renderedIndentFor(page.lines[0], indentWidth, PageLayout::Geometry::Alignment::RIGHT), 4);
  EXPECT_TRUE(PageLayout::isNaturalAlignment(PageLayout::Geometry::Alignment::RIGHT, true));
}

TEST(TxtPageLayout, RightAlignedNeutralPrefixUsesFinalLineDirectionForIndent) {
  const std::string source = "(" "\xD7\x90" "ab";
  const int indentWidth = PageLayout::indentWidthForSpaces(2, 2);
  const PageLayout::Geometry rightAligned{10, 10, 0, indentWidth, PageLayout::Geometry::Alignment::RIGHT,
                                          startsWithRtlForTest};
  const auto page = loadPage(source, 0, 1, 5, rightAligned);

  ASSERT_TRUE(page.loaded);
  ASSERT_EQ(page.lines.size(), 1u);
  EXPECT_EQ(page.lines[0].text, "(");
  EXPECT_EQ(page.nextOffset, 1u);
  EXPECT_EQ(renderedIndentFor(page.lines[0], indentWidth, PageLayout::Geometry::Alignment::RIGHT), 0);
}

TEST(TxtPageLayout, ParagraphGeometryKeepsSourceRangesContiguousAcrossPages) {
  const std::string source = "one two\n\nthree four five\nsix";
  const PageLayout::Geometry geometry{25, 10, 5, PageLayout::indentWidthForSpaces(2, 2)};
  size_t offset = 0;
  size_t consumedEnd = 0;
  std::string reconstructed;

  while (offset < source.size()) {
    const auto page = loadPage(source, offset, 2, 10, geometry);
    ASSERT_TRUE(page.loaded);
    ASSERT_GT(page.nextOffset, offset);
    ASSERT_EQ(page.lines.front().sourceStart, consumedEnd);
    for (const auto& line : page.lines) {
      EXPECT_EQ(line.sourceStart, consumedEnd);
      EXPECT_LE(line.sourceStart, line.sourceEnd);
      EXPECT_LE(line.sourceEnd, source.size());
      reconstructed.append(source, line.sourceStart, line.sourceEnd - line.sourceStart);
      consumedEnd = line.sourceEnd;
    }
    EXPECT_EQ(page.nextOffset, consumedEnd);
    offset = page.nextOffset;
  }

  EXPECT_EQ(offset, source.size());
  EXPECT_EQ(consumedEnd, source.size());
  EXPECT_EQ(reconstructed, source);
}

TEST(TxtPageLayout, TrackingBoundaryCountMatchesRendererSpaceAndCombiningRules) {
  const auto count = [](const char* text) {
    return PageLayout::trackingBoundaryCount([&](int tracking) { return measureWithTracking(text, tracking); });
  };

  EXPECT_EQ(count("AB"), 1);
  EXPECT_EQ(count("A B"), 0);
  EXPECT_EQ(count("A\xC2\xA0" "B"), 0);
  EXPECT_EQ(count("A\xE3\x80\x80" "B"), 0);
  EXPECT_EQ(count("A\xCC\x81" "B"), 1);
  EXPECT_EQ(count("\xD8\xA8\xD9\x8E" "\xD8\xAA"), 1);
}

TEST(TxtPageLayout, JustificationTrackingFitsAvailableWidthAndCapsAtInt8Range) {
  EXPECT_EQ(PageLayout::justifiedTracking(100, 40, 10), 6);
  EXPECT_EQ(PageLayout::justifiedTracking(100, 91, 4), 2);
  EXPECT_EQ(PageLayout::justifiedTracking(10000, 0, 1), 127);
  EXPECT_EQ(PageLayout::justifiedTracking(100, 40, 0), 0);
  EXPECT_EQ(PageLayout::justifiedTracking(39, 40, 5), 0);

  const int textWidth = 91;
  const int tracking = PageLayout::justifiedTracking(100, textWidth, 4);
  EXPECT_LE(textWidth + tracking * 4, 100);
}

TEST(TxtPageLayout, JustificationRequiresALtrSoftWrapThatIsNotPageFinal) {
  PageLine line;
  EXPECT_TRUE(PageLayout::shouldJustifyLine(line, false, PageLayout::Geometry::Alignment::JUSTIFIED, false));
  EXPECT_FALSE(PageLayout::shouldJustifyLine(line, true, PageLayout::Geometry::Alignment::JUSTIFIED, false));
  line.paragraphEnd = true;
  EXPECT_FALSE(PageLayout::shouldJustifyLine(line, false, PageLayout::Geometry::Alignment::JUSTIFIED, false));
  line.paragraphEnd = false;
  EXPECT_FALSE(PageLayout::shouldJustifyLine(line, false, PageLayout::Geometry::Alignment::LEFT, false));
  EXPECT_FALSE(PageLayout::shouldJustifyLine(line, false, PageLayout::Geometry::Alignment::JUSTIFIED, true));
}

TEST(TxtPageLayout, PaginationMarksTheSameSoftWrapAndFinalLineUsedByJustification) {
  const auto page = loadPage("abcdef\n", 0, 2, 3);

  ASSERT_TRUE(page.loaded);
  ASSERT_EQ(page.lines.size(), 2u);
  EXPECT_EQ(page.lines[0].text, "abc");
  EXPECT_FALSE(page.lines[0].paragraphEnd);
  EXPECT_TRUE(PageLayout::shouldJustifyLine(page.lines[0], false, PageLayout::Geometry::Alignment::JUSTIFIED,
                                             false));
  EXPECT_EQ(page.lines[1].text, "def");
  EXPECT_TRUE(page.lines[1].paragraphEnd);
  EXPECT_FALSE(PageLayout::shouldJustifyLine(page.lines[1], true, PageLayout::Geometry::Alignment::JUSTIFIED,
                                              false));
}

TEST(TxtPageLayout, BinarySearchMeasuresPrefixesLogarithmicallyForLongLines) {
  const std::string source(4096, 'a');
  size_t measurements = 0;
  const auto page = loadPageWithAdvance(source, 0, 1, 32, PageLayout::Geometry{}, [&measurements](const char* text) {
    ++measurements;
    return static_cast<int>(std::char_traits<char>::length(text));
  });

  ASSERT_TRUE(page.loaded);
  EXPECT_EQ(page.lines[0].text, std::string(32, 'a'));
  EXPECT_LT(measurements, 32u);
}
