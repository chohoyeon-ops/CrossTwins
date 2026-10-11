#include <HalStorage.h>
#include <TxtLayoutInvalidation.h>
#include <TxtPageEstimate.h>
#include <TxtPageMetrics.h>
#include <TxtPageReader.h>
#include <TxtProgressFile.h>
#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "GfxRenderer.h"
#include "Txt.h"
#include "src/activities/reader/ReaderMenuCapabilities.h"
#include "src/activities/settings/TextSettingsCapabilities.h"

namespace {
bool readPage(TxtPageReader& pageReader, const Txt& txt, const GfxRenderer& renderer, const size_t offset,
              const int lineLimit, const int viewportWidth, std::vector<std::string>& lines, size_t& nextOffset,
              const bool hyphenationEnabled = false) {
  return pageReader.loadPage(txt, renderer, 1, viewportWidth, lineLimit, offset, lines, nextOffset, hyphenationEnabled);
}

bool readPageWithMetrics(TxtPageReader& pageReader, const Txt& txt, const GfxRenderer& renderer, const size_t offset,
                         const int viewportWidth, const int viewportHeight, const int lineAdvance,
                         std::vector<std::string>& lines, size_t& nextOffset) {
  return readPage(pageReader, txt, renderer, offset, TxtPageMetrics::linesPerPage(viewportHeight, lineAdvance),
                  viewportWidth, lines, nextOffset);
}

bool isContinuationByte(const unsigned char byte) { return (byte & 0xC0) == 0x80; }

void writeU16(uint8_t* bytes, uint16_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(uint8_t* bytes, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) {
    bytes[i] = static_cast<uint8_t>(value);
    value >>= 8;
  }
}

void writeU64(uint8_t* bytes, uint64_t value) {
  for (size_t i = 0; i < 8; ++i) {
    bytes[i] = static_cast<uint8_t>(value);
    value >>= 8;
  }
}

uint64_t readU64(const uint8_t* bytes) {
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
  return value;
}

uint32_t checksum(const uint8_t* bytes, size_t length) {
  uint32_t value = 0xFFFFFFFFu;
  for (size_t i = 0; i < length; ++i) {
    value ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1u) ? 0xEDB88320u : 0u);
  }
  return ~value;
}

uint64_t pathFingerprint(const std::string& path) {
  uint64_t value = 14695981039346656037ull;
  for (const unsigned char byte : path) {
    value ^= byte;
    value *= 1099511628211ull;
  }
  return value;
}

std::vector<uint8_t> makeLegacyCheckpoint(const Txt& txt, const uint32_t generation, const size_t offset) {
  std::vector<uint8_t> bytes(44, 0);
  bytes[0] = 'T';
  bytes[1] = 'X';
  bytes[2] = 'P';
  bytes[3] = 'R';
  writeU16(bytes.data() + 4, 1);
  writeU16(bytes.data() + 6, 44);
  writeU32(bytes.data() + 8, generation);
  writeU64(bytes.data() + 12, pathFingerprint(txt.getPath()));
  writeU64(bytes.data() + 20, txt.getFileSize());
  writeU64(bytes.data() + 28, offset);
  writeU32(bytes.data() + 40, checksum(bytes.data(), 40));
  return bytes;
}

std::vector<uint8_t> makeCheckpointFixture(const Txt& txt, const uint16_t version, const uint32_t generation,
                                           const size_t offset, const uint64_t displayOrdinal,
                                           const std::array<size_t, 3>& previousStarts, const size_t previousCount,
                                           const bool hyphenationEnabled = false) {
  std::vector<uint8_t> bytes(84, 0);
  bytes[0] = 'T';
  bytes[1] = 'X';
  bytes[2] = 'P';
  bytes[3] = 'R';
  writeU16(bytes.data() + 4, version);
  writeU16(bytes.data() + 6, 84);
  writeU32(bytes.data() + 8, generation);
  writeU64(bytes.data() + 12, pathFingerprint(txt.getPath()));
  writeU64(bytes.data() + 20, txt.getFileSize());
  writeU64(bytes.data() + 28, offset);
  writeU32(bytes.data() + 36, checksum(bytes.data(), 36));
  writeU64(bytes.data() + 40, displayOrdinal);
  bytes[48] = static_cast<uint8_t>(previousCount);
  bytes[49] = hyphenationEnabled ? 1 : 0;
  for (size_t i = 0; i < previousCount && i < previousStarts.size(); ++i) {
    writeU64(bytes.data() + 56 + i * 8, previousStarts[i]);
  }
  writeU32(bytes.data() + 80, checksum(bytes.data() + 40, 40));
  return bytes;
}

void updateCheckpointMetadataChecksum(std::vector<uint8_t>& bytes) {
  writeU32(bytes.data() + 80, checksum(bytes.data() + 40, 40));
}
}  // namespace

TEST(TxtReaderOptionsTest, ExposesOnlyLayoutControlsImplementedByTheOnDemandReader) {
  using textsettings::LayoutControl;
  constexpr textsettings::Profile profile = textsettings::Profile::Txt;
  constexpr LayoutControl expected[] = {LayoutControl::LineSpacing, LayoutControl::Alignment,
                                        LayoutControl::ScreenMargin};

  ASSERT_EQ(textsettings::layoutControlCount(profile), std::size(expected));
  for (size_t i = 0; i < std::size(expected); ++i) {
    EXPECT_EQ(textsettings::layoutControlAt(profile, i), expected[i]);
  }
  EXPECT_TRUE(textsettings::hasLayoutControl(profile, LayoutControl::LineSpacing));
  EXPECT_TRUE(textsettings::hasLayoutControl(profile, LayoutControl::Alignment));
  EXPECT_TRUE(textsettings::hasLayoutControl(profile, LayoutControl::ScreenMargin));
  EXPECT_FALSE(textsettings::hasLayoutControl(profile, LayoutControl::WordSpacing));
  EXPECT_FALSE(textsettings::hasLayoutControl(profile, LayoutControl::CharacterSpacing));
  EXPECT_FALSE(textsettings::hasLayoutControl(profile, LayoutControl::ParagraphSpacing));
  EXPECT_FALSE(textsettings::hasLayoutControl(profile, LayoutControl::ParagraphIndentation));
}

TEST(TxtReaderOptionsTest, KeepsFontAndSizeTabsAndExposesHyphenationThenAntiAliasingInStyle) {
  using textsettings::StyleControl;
  using textsettings::Tab;
  constexpr textsettings::Profile profile = textsettings::Profile::Txt;
  constexpr Tab expectedTabs[] = {Tab::Font, Tab::Size, Tab::Layout, Tab::Style};

  ASSERT_EQ(textsettings::tabCount(profile), std::size(expectedTabs));
  for (size_t i = 0; i < std::size(expectedTabs); ++i) {
    EXPECT_EQ(textsettings::tabAt(profile, i), expectedTabs[i]);
  }
  EXPECT_EQ(textsettings::tabAt(profile, std::size(expectedTabs)), Tab::Count);
  EXPECT_FALSE(textsettings::hasLayoutPreview(profile));
  constexpr textsettings::Profile epubProfile = textsettings::Profile::Epub;
  EXPECT_TRUE(textsettings::hasLayoutPreview(epubProfile));
  ASSERT_EQ(textsettings::styleControlCount(epubProfile), 3u);
  EXPECT_EQ(textsettings::styleControlAt(epubProfile, 0), StyleControl::Hyphenation);
  EXPECT_EQ(textsettings::styleControlAt(epubProfile, 1), StyleControl::EmbeddedStyle);
  EXPECT_EQ(textsettings::styleControlAt(epubProfile, 2), StyleControl::AntiAliasing);
  EXPECT_FALSE(textsettings::hasStyleControl(epubProfile, StyleControl::FocusReading));
  ASSERT_EQ(textsettings::styleControlCount(profile), 2u);
  EXPECT_EQ(textsettings::styleControlAt(profile, 0), StyleControl::Hyphenation);
  EXPECT_EQ(textsettings::styleControlAt(profile, 1), StyleControl::AntiAliasing);
  EXPECT_TRUE(textsettings::hasStyleControl(profile, StyleControl::Hyphenation));
  EXPECT_TRUE(textsettings::hasStyleControl(profile, StyleControl::AntiAliasing));
  EXPECT_FALSE(textsettings::hasStyleControl(profile, StyleControl::FocusReading));
  EXPECT_FALSE(textsettings::hasStyleControl(profile, StyleControl::EmbeddedStyle));
}

TEST(TxtReaderOptionsTest, OffersOnlyLeftCenterAndRightAlignment) {
  using textsettings::AlignmentOption;
  constexpr textsettings::Profile profile = textsettings::Profile::Txt;
  constexpr AlignmentOption expected[] = {AlignmentOption::Left, AlignmentOption::Center, AlignmentOption::Right};

  ASSERT_EQ(textsettings::alignmentOptionCount(profile), std::size(expected));
  for (size_t i = 0; i < std::size(expected); ++i) {
    EXPECT_EQ(textsettings::alignmentOptionAt(profile, i), expected[i]);
  }
  EXPECT_EQ(textsettings::alignmentOptionAt(profile, std::size(expected)), AlignmentOption::Count);
}

TEST(TxtReaderOptionsTest, ShowsOnlyReaderMenuActionsWithWorkingTxtHandlers) {
  using readermenu::Action;
  constexpr readermenu::Profile profile = readermenu::Profile::Txt;
  constexpr Action expected[] = {Action::NIGHT_MODE, Action::ROTATE_SCREEN, Action::SCREENSHOT,
                                 Action::DISPLAY_QR, Action::GO_HOME,       Action::TEXT_SETTINGS};

  ASSERT_EQ(readermenu::actionCount(profile), std::size(expected));
  for (size_t i = 0; i < std::size(expected); ++i) {
    EXPECT_EQ(readermenu::actionAt(profile, i), expected[i]);
    EXPECT_TRUE(readermenu::supportsAction(profile, expected[i]));
  }
  EXPECT_EQ(readermenu::actionAt(profile, std::size(expected)), Action::Count);
  EXPECT_FALSE(readermenu::supportsAction(profile, Action::SELECT_CHAPTER));
  EXPECT_FALSE(readermenu::supportsAction(profile, Action::FOOTNOTES));
  EXPECT_FALSE(readermenu::supportsAction(profile, Action::FRONTLIGHT));
  EXPECT_FALSE(readermenu::supportsAction(profile, Action::GO_TO_PERCENT));
  EXPECT_FALSE(readermenu::supportsAction(profile, Action::AUTO_PAGE_TURN));
  EXPECT_FALSE(readermenu::supportsAction(profile, Action::BOOKMARKS));
  EXPECT_FALSE(readermenu::supportsAction(profile, Action::TOGGLE_BOOKMARK));
  EXPECT_FALSE(readermenu::supportsAction(profile, Action::SYNC));
  EXPECT_FALSE(readermenu::supportsAction(profile, Action::DELETE_CACHE));
  EXPECT_FALSE(readermenu::supportsAction(profile, Action::DICTIONARY));
}

TEST(TxtPageMetricsTest, NormalSpacingIsTheBaselineAndTighterOrWiderSpacingChangesFit) {
  GfxRenderer renderer;
  const int normalAdvance = TxtPageMetrics::effectiveLineAdvance(renderer.getLineHeight(1, 1.0f));
  const int tightAdvance = TxtPageMetrics::effectiveLineAdvance(renderer.getLineHeight(1, 0.95f));
  const int wideAdvance = TxtPageMetrics::effectiveLineAdvance(renderer.getLineHeight(1, 1.2f));

  EXPECT_EQ(normalAdvance, 16);
  EXPECT_EQ(TxtPageMetrics::linesPerPage(75, normalAdvance), 4);
  EXPECT_LT(tightAdvance, normalAdvance);
  EXPECT_EQ(TxtPageMetrics::linesPerPage(75, tightAdvance), 5);
  EXPECT_GT(wideAdvance, normalAdvance);
  EXPECT_EQ(TxtPageMetrics::linesPerPage(75, wideAdvance), 3);
  EXPECT_EQ(TxtPageMetrics::effectiveLineAdvance(0), 1);
  EXPECT_EQ(TxtPageMetrics::linesPerPage(0, 16), 1);
}

TEST(TxtPageMetricsTest, PageEndsAndRenderedLinesStayInParityForEachSpacing) {
  const std::string content = "first\nsecond\nthird\nfourth\nfifth\nsixth\nseventh\neighth\n";
  const std::vector<std::string> expectedLines = {"first", "second", "third",   "fourth",
                                                  "fifth", "sixth",  "seventh", "eighth"};
  const std::array<float, 3> spacingCompressions = {0.95f, 1.0f, 1.2f};
  constexpr int CONTENT_TOP = 11;
  constexpr int VIEWPORT_HEIGHT = 75;

  for (const float compression : spacingCompressions) {
    Txt txt(content);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    const int lineAdvance = TxtPageMetrics::effectiveLineAdvance(renderer.getLineHeight(1, compression));
    const int linesPerPage = TxtPageMetrics::linesPerPage(VIEWPORT_HEIGHT, lineAdvance);
    std::vector<std::string> renderedLines;
    std::vector<std::string> pageLines;
    std::string consumed;
    size_t pageStart = 0;
    size_t pageCount = 0;

    while (pageStart < content.size()) {
      size_t pageEnd = pageStart;
      ASSERT_TRUE(readPageWithMetrics(pageReader, txt, renderer, pageStart, 100, VIEWPORT_HEIGHT, lineAdvance,
                                      pageLines, pageEnd));
      ASSERT_GT(pageEnd, pageStart);
      ASSERT_LE(pageLines.size(), static_cast<size_t>(linesPerPage));
      for (size_t i = 0; i < pageLines.size(); ++i) {
        const int y = CONTENT_TOP + static_cast<int>(i) * lineAdvance;
        EXPECT_LT(y, CONTENT_TOP + VIEWPORT_HEIGHT);
        EXPECT_LE(y + lineAdvance, CONTENT_TOP + VIEWPORT_HEIGHT);
        renderedLines.push_back(pageLines[i]);
      }
      consumed.append(content, pageStart, pageEnd - pageStart);
      pageStart = pageEnd;
      ++pageCount;
    }

    EXPECT_EQ(consumed, content);
    EXPECT_EQ(renderedLines, expectedLines);
    EXPECT_EQ(pageCount,
              (expectedLines.size() + static_cast<size_t>(linesPerPage) - 1) / static_cast<size_t>(linesPerPage));
  }
}

TEST(TxtPageMetricsTest, FirstMenuBeforeFirstRenderInvalidatesAtRestoredDeepAnchor) {
  Storage.clear();
  std::string content;
  content.reserve(96 * 1024);
  for (size_t line = 0; line < 48 * 1024; ++line) content += "x\n";
  constexpr size_t ANCHOR = 24 * 1024;
  Txt txt(content, "/books/spacing.txt", "/.crosspoint/txt_spacing");

  TxtProgressState saved;
  saved.pageStartOffset = ANCHOR;
  saved.hasDisplayOrdinal = true;
  saved.displayOrdinal = 301;
  saved.previousPageStarts = {ANCHOR - 6, ANCHOR - 4, ANCHOR - 2};
  saved.previousPageCount = 3;
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, saved));

  // Opening Reader Options before the first reader render initializes from
  // the checkpoint exactly as initializeReader() does.
  TxtProgressFile beforeMenu;
  TxtProgressState restoredAtOpen;
  ASSERT_TRUE(beforeMenu.load(txt, restoredAtOpen));
  EXPECT_EQ(restoredAtOpen.pageStartOffset, ANCHOR);
  ASSERT_TRUE(restoredAtOpen.hasDisplayOrdinal);
  EXPECT_EQ(restoredAtOpen.displayOrdinal, 301u);

  TxtPageHistory history;
  ASSERT_TRUE(history.resetWithPrevious(restoredAtOpen.previousPageStarts, restoredAtOpen.previousPageCount,
                                        restoredAtOpen.pageStartOffset));
  TxtPageEstimate estimate;
  ASSERT_TRUE(estimate.observePage(ANCHOR - 2, ANCHOR));
  ASSERT_GT(estimate.bytesPerPage(), 0u);

  // This is the same production helper used when settings return, orientation
  // changes, or the reader exits with a layout change still pending.
  size_t displayOrdinal = restoredAtOpen.displayOrdinal;
  bool hasDisplayOrdinal = restoredAtOpen.hasDisplayOrdinal;
  txt.reads.clear();
  const TxtProgressState reset = txtlayout::invalidateAtVisibleAnchor(history, estimate, history.currentOffset(),
                                                                      displayOrdinal, hasDisplayOrdinal);
  EXPECT_TRUE(txt.reads.empty());
  EXPECT_EQ(reset.pageStartOffset, ANCHOR);
  EXPECT_FALSE(reset.hasDisplayOrdinal);
  EXPECT_EQ(reset.previousPageCount, 0u);
  ASSERT_TRUE(progress.save(txt, reset));
  EXPECT_EQ(displayOrdinal, 0u);
  EXPECT_FALSE(hasDisplayOrdinal);
  EXPECT_EQ(history.currentOffset(), ANCHOR);
  EXPECT_EQ(history.currentPage(), 0u);
  EXPECT_FALSE(history.hasForward());
  EXPECT_FALSE(history.retreat());
  EXPECT_EQ(estimate.bytesPerPage(), 2048u);
  EXPECT_EQ(estimate.pageNumberAt(ANCHOR), ANCHOR / 2048 + 1);

  txt.reads.clear();
  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, ANCHOR);
  EXPECT_FALSE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.previousPageCount, 0u);

  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = ANCHOR;
  const int lineAdvance = TxtPageMetrics::effectiveLineAdvance(renderer.getLineHeight(1, 1.2f));
  ASSERT_TRUE(readPageWithMetrics(pageReader, txt, renderer, ANCHOR, 100, 75, lineAdvance, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"x", "x", "x"}));
  EXPECT_EQ(nextOffset, ANCHOR + 6);
  ASSERT_EQ(txt.reads.size(), 3u);
  EXPECT_EQ(txt.reads[0], (std::pair<size_t, size_t>{ANCHOR, 1}));
  EXPECT_EQ(txt.reads[1], (std::pair<size_t, size_t>{ANCHOR, 1}));
  EXPECT_EQ(txt.reads[2], (std::pair<size_t, size_t>{ANCHOR, TxtPageReader::READ_WINDOW_SIZE}));
  for (const auto& [offset, length] : txt.reads) {
    EXPECT_GE(offset, ANCHOR);
    EXPECT_LE(length, TxtPageReader::READ_WINDOW_SIZE);
  }
}

TEST(TxtOnDemandTest, EveryIncludedNoLineStartMarkMovesWithItsPrecedingCjkLetter) {
  const std::string hangulA = "\xEA\xB0\x80";
  const std::string hangulNa = "\xEB\x82\x98";
  const std::string hangulDa = "\xEB\x8B\xA4";
  const std::vector<std::string> marks = {".",
                                          ",",
                                          ":",
                                          ";",
                                          "!",
                                          "?",
                                          ")",
                                          "]",
                                          "}",
                                          "\xE2\x80\x99",
                                          "\xE2\x80\x9D",
                                          "\xE3\x80\x81",
                                          "\xE3\x80\x82",
                                          "\xE3\x80\x89",
                                          "\xE3\x80\x8B",
                                          "\xE3\x80\x8D",
                                          "\xE3\x80\x8F",
                                          "\xE3\x80\x91",
                                          "\xEF\xBC\x81",
                                          "\xEF\xBC\x89",
                                          "\xEF\xBC\x8C",
                                          "\xEF\xBC\x8E",
                                          "\xEF\xBC\x9A",
                                          "\xEF\xBC\x9B",
                                          "\xEF\xBC\x9F",
                                          "\xEF\xBC\xBD",
                                          "\xEF\xBD\x9D"};

  for (const auto& mark : marks) {
    SCOPED_TRACE(mark);
    Txt txt(hangulA + hangulNa + mark + hangulDa);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;

    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{hangulA}));
    EXPECT_EQ(nextOffset, hangulA.size());
  }
}

TEST(TxtOnDemandTest, EveryIncludedNoLineEndMarkMovesWithFollowingCjkText) {
  const std::string hangulA = "\xEA\xB0\x80";
  const std::vector<std::string> marks = {"(",
                                          "[",
                                          "{",
                                          "\xE2\x80\x98",
                                          "\xE2\x80\x9C",
                                          "\xE3\x80\x88",
                                          "\xE3\x80\x8A",
                                          "\xE3\x80\x8C",
                                          "\xE3\x80\x8E",
                                          "\xE3\x80\x90",
                                          "\xEF\xBC\x88",
                                          "\xEF\xBC\xBB",
                                          "\xEF\xBD\x9B"};

  for (const auto& mark : marks) {
    SCOPED_TRACE(mark);
    Txt txt("a" + mark + hangulA);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;

    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 3, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{"a"}));
    EXPECT_EQ(nextOffset, 1u);
  }
}

TEST(TxtOnDemandTest, ExcludedPunctuationAndNonCjkBoundariesKeepBaselineSplits) {
  const std::string hangulA = "\xEA\xB0\x80";
  const std::string hangulNa = "\xEB\x82\x98";
  const std::string hangulDa = "\xEB\x8B\xA4";
  const std::vector<std::string> excludedStarts = {"'",
                                                   "\"",
                                                   "\xC2\xAB",
                                                   "\xC2\xBB",
                                                   "\xE2\x80\xA6",
                                                   "\xE2\x80\x93",
                                                   "\xE2\x80\x94",
                                                   "\xE3\x80\x94",
                                                   "\xE3\x80\x9B",
                                                   "\xE3\x81\x81"};
  for (const auto& mark : excludedStarts) {
    SCOPED_TRACE(mark);
    Txt txt(hangulA + hangulNa + mark + hangulDa);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{hangulA + hangulNa}));
    EXPECT_EQ(nextOffset, hangulA.size() + hangulNa.size());
  }

  const std::vector<std::string> excludedEnds = {
      "'",           "\"", "\xC2\xAB", "\xC2\xBB", "\xE2\x80\xA6", "\xE2\x80\x93", "\xE2\x80\x94", "\xE3\x80\x94",
      "\xE3\x80\x9B"};
  for (const auto& mark : excludedEnds) {
    SCOPED_TRACE(mark);
    Txt txt("a" + mark + hangulA);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 3, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{"a" + mark}));
    EXPECT_EQ(nextOffset, 1u + mark.size());
  }

  {
    Txt txt("abc.def");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 3, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{"abc"}));
    EXPECT_EQ(nextOffset, 3u);
  }
  {
    Txt txt(hangulA + "abc.def");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 5, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{hangulA + "abc"}));
    EXPECT_EQ(nextOffset, hangulA.size() + 3);
  }
}

TEST(TxtOnDemandTest, ReplacementEdgesWhitespaceAndMalformedUnitsFallBackSafely) {
  const std::string hangulA = "\xEA\xB0\x80";
  {
    Txt txt("a(" + hangulA + ",x");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{"a(" + hangulA}));
    EXPECT_EQ(nextOffset, 2u + hangulA.size());
  }
  {
    Txt txt(hangulA + "..x");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 2, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{hangulA}));
    EXPECT_EQ(nextOffset, hangulA.size());
  }
  {
    Txt txt(hangulA + "\t.x");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 3, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{hangulA + "\t"}));
    EXPECT_EQ(nextOffset, hangulA.size() + 1);
  }

  const std::vector<std::string> malformed = {"\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE0\x80\x80",
                                              "\x80",         "\xE2\x82",         "\xF0\x9F\x92"};
  for (const auto& unit : malformed) {
    SCOPED_TRACE(unit);
    Txt txt(unit + ".x");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 2, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{unit}));
    EXPECT_EQ(nextOffset, unit.size());
  }

  {
    Txt txt("\xE3\x81\x8B\xE3\x82\x99.x");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset));
    EXPECT_EQ(lines, (std::vector<std::string>{"\xE3\x81\x8B\xE3\x82\x99"}));
    EXPECT_EQ(nextOffset, 6u);
  }
}

TEST(TxtOnDemandTest, KoreanSplitIsOptInAndConsumesTheSelectedOriginalBytes) {
  const std::string content = "a \xEA\xB0\x80\xEB\x82\x98";
  Txt txt(content);
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;

  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"a"}));
  EXPECT_EQ(nextOffset, 2u);
  ASSERT_TRUE(readPage(pageReader, txt, renderer, nextOffset, 1, 4, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"\xEA\xB0\x80\xEB\x82\x98"}));
  EXPECT_EQ(nextOffset, content.size());

  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset, true));
  EXPECT_EQ(lines, (std::vector<std::string>{"a \xEA\xB0\x80"}));
  EXPECT_EQ(nextOffset, 5u);
  ASSERT_TRUE(readPage(pageReader, txt, renderer, nextOffset, 1, 4, lines, nextOffset, true));
  EXPECT_EQ(lines, (std::vector<std::string>{"\xEB\x82\x98"}));
  EXPECT_EQ(nextOffset, content.size());

  std::string reconstructed;
  size_t sourceStart = 0;
  while (sourceStart < content.size()) {
    ASSERT_TRUE(readPage(pageReader, txt, renderer, sourceStart, 1, 4, lines, nextOffset, true));
    ASSERT_GT(nextOffset, sourceStart);
    reconstructed.append(content, sourceStart, nextOffset - sourceStart);
    sourceStart = nextOffset;
  }
  EXPECT_EQ(reconstructed, content);
}

TEST(TxtOnDemandTest, KoreanSplitChoosesWidestMeasuredPrefixAndRetreatsToLegalBoundary) {
  const std::string hangul = "\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4";
  {
    Txt txt("a " + hangul);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 6, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a \xEA\xB0\x80\xEB\x82\x98"}));
    EXPECT_EQ(nextOffset, 8u);
  }

  // An emergency baseline break after '(' is illegal. The adapter can choose
  // the earlier legal Hangul/open-bracket boundary and bypass Phase 5.
  {
    Txt txt(
        "\xEA\xB0\x80("
        "\xEB\x82\x98");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 3, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"\xEA\xB0\x80"}));
    EXPECT_EQ(nextOffset, 3u);
  }
}

TEST(TxtOnDemandTest, KoreanSplitRespectsClosingOpeningAndConsecutivePunctuation) {
  const std::string a = "\xEA\xB0\x80";
  const std::string na = "\xEB\x82\x98";
  const std::string da = "\xEB\x8B\xA4";
  {
    Txt txt("a " + a + na + da + ".");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 8, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a " + a + na}));
    EXPECT_EQ(nextOffset, 8u);
  }
  {
    Txt txt("a " + a + "(" + na);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 5, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a " + a}));
    EXPECT_EQ(nextOffset, 5u);
  }
  {
    Txt txt("a " + a + "!!" + na);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a"}));
    EXPECT_EQ(nextOffset, 2u);
  }
}

TEST(TxtOnDemandTest, KoreanSplitHandlesCombiningMarksDigitsLatinBracketsAndHanjaPolicy) {
  const std::string a = "\xEA\xB0\x80";
  {
    Txt txt("a " + a + "\xCC\x81" + "\xEB\x82\x98");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 6, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a " + a + "\xCC\x81"}));
    EXPECT_EQ(nextOffset, 7u);
  }
  for (const std::string& suffix : {std::string("1\xEB\x82\x98"), std::string("B\xEB\x82\x98")}) {
    Txt txt("a " + a + suffix);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a " + a}));
    EXPECT_EQ(nextOffset, 5u);
  }
  {
    Txt txt("a \xE6\xBC\xA2\xE5\xAD\x97");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a"}));
    EXPECT_EQ(nextOffset, 2u);
  }
  {
    Txt txt("a " + a + "\xE6\xBC\xA2\xE5\xAD\x97");
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a " + a}));
    EXPECT_EQ(nextOffset, 5u);
  }
}

TEST(TxtOnDemandTest, KoreanSplitFallsBackForIncompleteOverLimitAndMalformedWords) {
  const std::string hangul = "\xEA\xB0\x80";
  {
    std::string exactly200Bytes;
    for (int i = 0; i < 66; ++i) exactly200Bytes += hangul;
    exactly200Bytes += "ab";
    ASSERT_EQ(exactly200Bytes.size(), 200u);
    Txt txt("a " + exactly200Bytes);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a " + hangul}));
    EXPECT_EQ(nextOffset, 5u);
  }
  {
    std::string overLimit;
    for (int i = 0; i < 67; ++i) overLimit += hangul;
    ASSERT_EQ(overLimit.size(), 201u);
    Txt txt("a " + overLimit);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a"}));
    EXPECT_EQ(nextOffset, 2u);
  }
  {
    Txt txt("a " + hangul + std::string("\xFF", 1) + hangul);
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset, true));
    EXPECT_EQ(lines, (std::vector<std::string>{"a"}));
    EXPECT_EQ(nextOffset, 2u);
  }
}

TEST(TxtOnDemandTest, KoreanSplitUsesNoContinuationReadAtTheEightKiBWindowEdge) {
  const size_t prefixSize = TxtPageReader::READ_WINDOW_SIZE - 3;
  Txt txt(std::string(prefixSize, 'a') + " \xEA\xB0\x80\xEB\x82\x98");
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;
  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, static_cast<int>(prefixSize), lines, nextOffset, true));
  EXPECT_EQ(lines, (std::vector<std::string>{std::string(prefixSize, 'a')}));
  EXPECT_EQ(nextOffset, prefixSize + 1);
  ASSERT_EQ(txt.reads.size(), 1u);
  EXPECT_EQ(txt.reads.front(), (std::pair<size_t, size_t>{0, TxtPageReader::READ_WINDOW_SIZE}));
}

TEST(TxtOnDemandTest, HyphenationSettingAddsNoEnglishSyntheticHyphenation) {
  Txt txt("a abcdef");
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;
  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, nextOffset, true));
  EXPECT_EQ(lines, (std::vector<std::string>{"a"}));
  EXPECT_EQ(nextOffset, 2u);
  ASSERT_TRUE(readPage(pageReader, txt, renderer, nextOffset, 1, 4, lines, nextOffset, true));
  EXPECT_EQ(lines, (std::vector<std::string>{"abcd"}));
  EXPECT_EQ(nextOffset, 6u);
  EXPECT_EQ(lines.front().find('-'), std::string::npos);
}

TEST(TxtOnDemandProgressTest, KoreanModeCheckpointBackForwardReplaysExactPagesAndOrdinal) {
  struct PageSample {
    size_t start;
    size_t end;
    std::vector<std::string> lines;
  };

  Storage.clear();
  const std::string content =
      "a \xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4\xEB\x9D\xBC\xEB\xA7\x88"
      "\xEB\xB0\x94\xEC\x82\xAC\xEC\x95\x84\xEC\x9E\x90\xEC\xB0\xA8\r\n"
      "z\n";
  Txt txt(content, "/books/korean-replay.txt", "/.crosspoint/txt_korean_replay");
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<PageSample> pages;
  std::string reconstructedSource;
  size_t sourceStart = 0;

  while (sourceStart < content.size()) {
    PageSample page{sourceStart, sourceStart, {}};
    ASSERT_TRUE(readPage(pageReader, txt, renderer, sourceStart, 1, 4, page.lines, page.end, true));
    ASSERT_GT(page.end, page.start);
    reconstructedSource.append(content, page.start, page.end - page.start);
    sourceStart = page.end;
    pages.push_back(std::move(page));
  }
  ASSERT_GE(pages.size(), 5u);
  EXPECT_EQ(reconstructedSource, content);

  TxtPageHistory history;
  history.reset();
  for (size_t i = 0; i + 1 < pages.size(); ++i) {
    ASSERT_EQ(history.currentOffset(), pages[i].start);
    ASSERT_TRUE(history.advance(pages[i].end));
  }
  ASSERT_EQ(history.currentOffset(), pages.back().start);

  TxtProgressState saved;
  saved.pageStartOffset = history.currentOffset();
  saved.hasDisplayOrdinal = true;
  saved.displayOrdinal = pages.size();
  saved.previousPageCount = history.copyPreviousOffsets(saved.previousPageStarts);
  ASSERT_EQ(saved.previousPageCount, TxtProgressState::PREVIOUS_PAGE_CAPACITY);
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, saved, true));
  const auto& record = Storage.files["/.crosspoint/txt_korean_replay/progress0.bin"];
  ASSERT_EQ(record[4], 4u);
  EXPECT_EQ(record[49], 1u);

  txt.reads.clear();
  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored, true));
  EXPECT_EQ(restored.pageStartOffset, pages.back().start);
  ASSERT_TRUE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.displayOrdinal, pages.size());
  EXPECT_EQ(restored.previousPageCount, TxtProgressState::PREVIOUS_PAGE_CAPACITY);
  EXPECT_EQ(restored.previousPageStarts, saved.previousPageStarts);
  ASSERT_EQ(txt.reads.size(), 1u);
  EXPECT_EQ(txt.reads[0].first, pages.back().start);

  TxtPageHistory replay;
  ASSERT_TRUE(
      replay.resetWithPrevious(restored.previousPageStarts, restored.previousPageCount, restored.pageStartOffset));
  ASSERT_TRUE(replay.retreat());
  size_t replayOrdinal = restored.displayOrdinal - 1;
  const PageSample& previousPage = pages[pages.size() - 2];
  EXPECT_EQ(replay.currentOffset(), previousPage.start);
  EXPECT_EQ(replayOrdinal, pages.size() - 1);
  std::vector<std::string> replayedLines;
  size_t replayedEnd = 0;
  ASSERT_TRUE(readPage(pageReader, txt, renderer, replay.currentOffset(), 1, 4, replayedLines, replayedEnd, true));
  EXPECT_EQ(replayedLines, previousPage.lines);
  EXPECT_EQ(replayedEnd, previousPage.end);

  ASSERT_TRUE(replay.forward());
  ++replayOrdinal;
  const PageSample& currentPage = pages.back();
  EXPECT_EQ(replay.currentOffset(), currentPage.start);
  EXPECT_EQ(replayOrdinal, restored.displayOrdinal);
  ASSERT_TRUE(readPage(pageReader, txt, renderer, replay.currentOffset(), 1, 4, replayedLines, replayedEnd, true));
  EXPECT_EQ(replayedLines, currentPage.lines);
  EXPECT_EQ(replayedEnd, currentPage.end);
}

TEST(TxtOnDemandProgressTest, KoreanModeChangeInvalidatesHistoryAtDeepVisibleAnchorWithoutScanning) {
  Storage.clear();
  std::string content;
  content.reserve(128 * 1024);
  for (size_t line = 0; line < 64 * 1024; ++line) content += "x\n";
  constexpr size_t ANCHOR = 60 * 1024;
  Txt txt(content, "/books/mode-anchor.txt", "/.crosspoint/txt_mode_anchor");

  TxtProgressState previousLayout;
  previousLayout.pageStartOffset = ANCHOR;
  previousLayout.hasDisplayOrdinal = true;
  previousLayout.displayOrdinal = 301;
  previousLayout.previousPageStarts = {ANCHOR - 6, ANCHOR - 4, ANCHOR - 2};
  previousLayout.previousPageCount = 3;
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, previousLayout, false));

  TxtPageHistory history;
  ASSERT_TRUE(history.resetWithPrevious(previousLayout.previousPageStarts, previousLayout.previousPageCount, ANCHOR));
  TxtPageEstimate estimate;
  ASSERT_TRUE(estimate.observePage(ANCHOR - 2, ANCHOR));
  size_t displayOrdinal = previousLayout.displayOrdinal;
  bool hasDisplayOrdinal = previousLayout.hasDisplayOrdinal;
  txt.reads.clear();

  const TxtProgressState reset =
      txtlayout::invalidateAtVisibleAnchor(history, estimate, ANCHOR, displayOrdinal, hasDisplayOrdinal);
  EXPECT_TRUE(txt.reads.empty());
  EXPECT_EQ(reset.pageStartOffset, ANCHOR);
  EXPECT_FALSE(reset.hasDisplayOrdinal);
  EXPECT_EQ(reset.previousPageCount, 0u);
  EXPECT_EQ(history.currentOffset(), ANCHOR);
  EXPECT_FALSE(history.retreat());
  EXPECT_FALSE(history.hasForward());

  ASSERT_TRUE(progress.save(txt, reset, true));
  const auto& changedMode = Storage.files["/.crosspoint/txt_mode_anchor/progress1.bin"];
  EXPECT_EQ(changedMode[4], 4u);
  EXPECT_EQ(changedMode[49], 1u);
  txt.reads.clear();
  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored, true));
  EXPECT_EQ(restored.pageStartOffset, ANCHOR);
  EXPECT_FALSE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.previousPageCount, 0u);
  ASSERT_EQ(txt.reads.size(), 2u);
  for (const auto& read : txt.reads) {
    EXPECT_EQ(read.first, ANCHOR);
    EXPECT_EQ(read.second, 1u);
  }
}

TEST(TxtOnDemandTest, PunctuationPagesReconstructContinuousSourceWithoutGaps) {
  const std::string content = "\xEA\xB0\x80\xEB\x82\x98.\xEB\x8B\xA4\xEB\x9D\xBC!\xEB\xA7\x88\xEB\xB0\x94";
  Txt txt(content);
  GfxRenderer renderer;
  TxtPageReader pageReader;
  TxtPageHistory history;
  history.reset();
  std::vector<std::string> lines;
  std::string reconstructed;
  std::string rendered;
  size_t current = 0;
  size_t pageCount = 0;

  while (current < content.size()) {
    size_t next = current;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, current, 1, 4, lines, next));
    ASSERT_GT(next, current);
    ASSERT_EQ(history.currentOffset(), current);
    for (const auto& line : lines) rendered += line;
    reconstructed.append(content, current, next - current);
    current = next;
    ++pageCount;
    if (current < content.size()) ASSERT_TRUE(history.advance(current));
  }

  EXPECT_EQ(reconstructed, content);
  EXPECT_EQ(rendered, content);
  EXPECT_EQ(pageCount, 5u);
  EXPECT_EQ(history.currentOffset(), content.size() - 6u);
}

TEST(TxtOnDemandTest, ThreeAndFourByteUnitsAtEveryWindowAlignmentKeepReadsBounded) {
  const std::string threeByteUnit = "\xEA\xB0\x80";
  const std::string fourByteUnit = "\xF0\x9F\x8C\x8D";
  const auto checkAlignment = [](const std::string& unit, const size_t startOffset) {
    const size_t prefixLength = startOffset;
    const std::string prefix(prefixLength, 'x');
    Txt txt(prefix + unit + ".\n" + std::string(TxtPageReader::READ_WINDOW_SIZE, 'z'));
    GfxRenderer renderer;
    TxtPageReader pageReader;
    std::vector<std::string> lines;
    size_t nextOffset = 0;

    ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, static_cast<int>(prefixLength + 2), lines, nextOffset));
    const bool unitFits = startOffset + unit.size() <= TxtPageReader::READ_WINDOW_SIZE;
    EXPECT_EQ(lines, (std::vector<std::string>{unitFits ? prefix + unit : prefix}));
    EXPECT_EQ(nextOffset, unitFits ? TxtPageReader::READ_WINDOW_SIZE : startOffset);
    ASSERT_EQ(txt.reads.size(), 1u);
    EXPECT_EQ(txt.reads[0], (std::pair<size_t, size_t>{0, TxtPageReader::READ_WINDOW_SIZE}));
  };

  for (const size_t startOffset : {TxtPageReader::READ_WINDOW_SIZE - 3, TxtPageReader::READ_WINDOW_SIZE - 2,
                                   TxtPageReader::READ_WINDOW_SIZE - 1}) {
    SCOPED_TRACE(startOffset);
    checkAlignment(threeByteUnit, startOffset);
  }
  for (const size_t startOffset : {TxtPageReader::READ_WINDOW_SIZE - 4, TxtPageReader::READ_WINDOW_SIZE - 3,
                                   TxtPageReader::READ_WINDOW_SIZE - 2, TxtPageReader::READ_WINDOW_SIZE - 1}) {
    SCOPED_TRACE(startOffset);
    checkAlignment(fourByteUnit, startOffset);
  }
}

TEST(TxtOnDemandTest, FirstPageReadsOneBoundedWindowAndDoesNotReachEof) {
  std::string content;
  content.reserve(32 * 1024);
  for (int i = 0; i < 16 * 1024; ++i) content += "x\n";
  Txt txt(content);
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;

  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 2, 100, lines, nextOffset));

  ASSERT_EQ(txt.reads.size(), 1u);
  EXPECT_EQ(txt.reads[0].first, 0u);
  EXPECT_EQ(txt.reads[0].second, TxtPageReader::READ_WINDOW_SIZE);
  EXPECT_EQ(lines, (std::vector<std::string>{"x", "x"}));
  EXPECT_EQ(nextOffset, 4u);
  EXPECT_LT(nextOffset, txt.getFileSize());
}

TEST(TxtOnDemandTest, NextPagesConsumeContinuousSourceRangesAcrossLineEndingsAndBlankLines) {
  const std::string content = "alpha\nbeta\r\ngamma\n\nlast";
  Txt txt(content);
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  std::vector<std::string> renderedLines;
  size_t offset = 0;
  std::string consumed;

  while (offset < txt.getFileSize()) {
    const size_t pageStart = offset;
    size_t nextOffset = offset;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, offset, 1, 100, lines, nextOffset));
    ASSERT_EQ(lines.size(), 1u);
    renderedLines.push_back(lines.front());
    ASSERT_GT(nextOffset, pageStart);
    consumed.append(content, pageStart, nextOffset - pageStart);
    offset = nextOffset;
  }

  EXPECT_EQ(offset, content.size());
  EXPECT_EQ(consumed, content);
  EXPECT_EQ(renderedLines, (std::vector<std::string>{"alpha", "beta", "gamma", "", "last"}));
  ASSERT_EQ(txt.reads.size(), renderedLines.size() + 1);
  EXPECT_EQ(txt.reads[4], (std::pair<size_t, size_t>{17, 1}));
  EXPECT_FALSE(readPage(pageReader, txt, renderer, offset, 1, 100, lines, offset));
  EXPECT_EQ(txt.reads.size(), renderedLines.size() + 1);
}

TEST(TxtOnDemandTest, WrappedAsciiSpaceIsConsumedExactlyOnce) {
  const std::string content = "abcd efgh\n";
  Txt txt(content);
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t firstEnd = 0;

  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, firstEnd));
  EXPECT_EQ(lines, (std::vector<std::string>{"abcd"}));
  EXPECT_EQ(firstEnd, 5u);

  size_t secondEnd = firstEnd;
  ASSERT_TRUE(readPage(pageReader, txt, renderer, firstEnd, 1, 4, lines, secondEnd));
  EXPECT_EQ(lines, (std::vector<std::string>{"efgh"}));
  EXPECT_EQ(secondEnd, content.size());
}

TEST(TxtOnDemandTest, MultibyteCharactersAreNotSplitAtPageStarts) {
  const std::string content = "\xEA\xB0\x80\xEB\x82\x98\n\xF0\x9F\x98\x80Z";
  Txt txt(content);
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t offset = 0;
  std::vector<size_t> starts;
  std::vector<std::string> renderedLines;

  while (offset < content.size()) {
    starts.push_back(offset);
    size_t nextOffset = offset;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, offset, 1, 1, lines, nextOffset));
    renderedLines.push_back(lines.front());
    ASSERT_GT(nextOffset, offset);
    offset = nextOffset;
  }

  EXPECT_EQ(renderedLines, (std::vector<std::string>{"\xEA\xB0\x80", "\xEB\x82\x98", "\xF0\x9F\x98\x80", "Z"}));
  for (const size_t start : starts) {
    if (start < content.size()) {
      EXPECT_FALSE(isContinuationByte(static_cast<unsigned char>(content[start])));
    }
  }
  EXPECT_EQ(offset, content.size());
}

TEST(TxtOnDemandTest, IncompleteUtf8AtWindowEdgeIsLeftForTheNextPage) {
  std::string content(TxtPageReader::READ_WINDOW_SIZE - 2, 'x');
  content += "\xEA\xB0\x80";
  Txt txt(content);
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;

  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 10000, lines, nextOffset));
  EXPECT_EQ(nextOffset, TxtPageReader::READ_WINDOW_SIZE - 2);
  EXPECT_EQ(txt.reads[0].second, TxtPageReader::READ_WINDOW_SIZE);
  EXPECT_EQ(static_cast<unsigned char>(content[nextOffset]), 0xEA);

  ASSERT_TRUE(readPage(pageReader, txt, renderer, nextOffset, 1, 10000, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"\xEA\xB0\x80"}));
  EXPECT_EQ(nextOffset, content.size());
}

TEST(TxtOnDemandTest, CrLfSplitAtTheReadWindowDoesNotCreateAnExtraBlankLine) {
  const std::string firstLine(TxtPageReader::READ_WINDOW_SIZE - 1, 'x');
  const std::string content = firstLine + "\r\nnext\n";
  Txt txt(content);
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;

  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 10000, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{firstLine}));
  EXPECT_EQ(nextOffset, TxtPageReader::READ_WINDOW_SIZE + 1);

  size_t finalOffset = nextOffset;
  ASSERT_TRUE(readPage(pageReader, txt, renderer, nextOffset, 1, 100, lines, finalOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"next"}));
  EXPECT_EQ(finalOffset, content.size());
  EXPECT_EQ(txt.reads.size(), 3u);
}

TEST(TxtOnDemandTest, TerminalCrLfSplitAtWindowEdgeIsConsumedWithItsLine) {
  const std::string content(TxtPageReader::READ_WINDOW_SIZE - 1, 'x');
  Txt txt(content + "\r\n");
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;

  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 10000, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{content}));
  EXPECT_EQ(nextOffset, txt.getFileSize());
  EXPECT_EQ(txt.reads.size(), 2u);
  EXPECT_FALSE(readPage(pageReader, txt, renderer, nextOffset, 1, 100, lines, nextOffset));
  EXPECT_EQ(txt.reads.size(), 2u);
}

TEST(TxtOnDemandTest, EmptyOnePageAndFinalLineWithoutNewlineAreSafe) {
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;

  Txt empty("");
  EXPECT_FALSE(readPage(pageReader, empty, renderer, 0, 1, 100, lines, nextOffset));
  EXPECT_TRUE(empty.reads.empty());
  EXPECT_EQ(nextOffset, 0u);

  Txt onePage("short\n");
  ASSERT_TRUE(readPage(pageReader, onePage, renderer, 0, 5, 100, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"short"}));
  EXPECT_EQ(nextOffset, onePage.getFileSize());
  EXPECT_FALSE(readPage(pageReader, onePage, renderer, nextOffset, 1, 100, lines, nextOffset));

  Txt noFinalNewline("final line");
  ASSERT_TRUE(readPage(pageReader, noFinalNewline, renderer, 0, 1, 100, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"final line"}));
  EXPECT_EQ(nextOffset, noFinalNewline.getFileSize());
}

TEST(TxtOnDemandTest, FailedSourceReadDoesNotAdvanceThePageOffset) {
  Txt txt("some text");
  txt.failReads = true;
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;

  EXPECT_FALSE(readPage(pageReader, txt, renderer, 0, 1, 100, lines, nextOffset));
  EXPECT_TRUE(lines.empty());
  EXPECT_EQ(nextOffset, 0u);

  txt.failReads = false;
  txt.shortRead = true;
  EXPECT_FALSE(readPage(pageReader, txt, renderer, 0, 1, 100, lines, nextOffset));
  EXPECT_TRUE(lines.empty());
  EXPECT_EQ(nextOffset, 0u);
}

TEST(TxtOnDemandTest, TruncatedMalformedUtf8AtEofStillMakesProgress) {
  Txt txt("\xE2\x82");
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t offset = 0;

  ASSERT_TRUE(readPage(pageReader, txt, renderer, offset, 1, 1, lines, offset));
  EXPECT_EQ(offset, txt.getFileSize());
  EXPECT_EQ(lines.size(), 1u);
}

TEST(TxtOnDemandTest, BackHistoryTracksSequentialStartsAndRepeatedBack) {
  TxtPageHistory history;
  history.reset();
  EXPECT_EQ(history.currentOffset(), 0u);
  EXPECT_FALSE(history.advance(0));

  for (size_t page = 1; page <= 3; ++page) {
    ASSERT_TRUE(history.advance(page * 10));
  }
  EXPECT_EQ(history.currentPage(), 3u);
  EXPECT_EQ(history.currentOffset(), 30u);

  ASSERT_TRUE(history.retreat());
  EXPECT_EQ(history.currentPage(), 2u);
  EXPECT_EQ(history.currentOffset(), 20u);
  ASSERT_TRUE(history.retreat());
  EXPECT_EQ(history.currentPage(), 1u);
  EXPECT_EQ(history.currentOffset(), 10u);
  ASSERT_TRUE(history.retreat());
  EXPECT_EQ(history.currentPage(), 0u);
  EXPECT_EQ(history.currentOffset(), 0u);
  for (int i = 0; i < 3; ++i) {
    EXPECT_FALSE(history.retreat());
    EXPECT_EQ(history.currentPage(), 0u);
    EXPECT_EQ(history.currentOffset(), 0u);
  }

  EXPECT_TRUE(history.hasForward());
  EXPECT_FALSE(history.advance(25));
  EXPECT_EQ(history.currentOffset(), 0u);
  ASSERT_TRUE(history.forward());
  EXPECT_EQ(history.currentPage(), 1u);
  EXPECT_EQ(history.currentOffset(), 10u);
  ASSERT_TRUE(history.forward());
  EXPECT_EQ(history.currentPage(), 2u);
  EXPECT_EQ(history.currentOffset(), 20u);
  ASSERT_TRUE(history.forward());
  EXPECT_EQ(history.currentPage(), 3u);
  EXPECT_EQ(history.currentOffset(), 30u);
  EXPECT_FALSE(history.hasForward());
}

TEST(TxtOnDemandTest, BackForwardReplaysExactVisitedPagesWithoutDuplicates) {
  Txt txt("first\nsecond\nthird\nfourth\n");
  GfxRenderer renderer;
  TxtPageReader pageReader;
  TxtPageHistory history;
  std::vector<std::string> lines;
  size_t nextOffset = 0;

  history.reset();
  ASSERT_TRUE(readPage(pageReader, txt, renderer, history.currentOffset(), 1, 100, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"first"}));
  ASSERT_TRUE(history.advance(nextOffset));
  ASSERT_TRUE(readPage(pageReader, txt, renderer, history.currentOffset(), 1, 100, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"second"}));
  ASSERT_TRUE(history.advance(nextOffset));
  ASSERT_TRUE(readPage(pageReader, txt, renderer, history.currentOffset(), 1, 100, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"third"}));
  ASSERT_TRUE(history.advance(nextOffset));
  ASSERT_TRUE(readPage(pageReader, txt, renderer, history.currentOffset(), 1, 100, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"fourth"}));

  ASSERT_TRUE(history.retreat());
  ASSERT_TRUE(history.retreat());
  EXPECT_EQ(history.currentOffset(), 6u);
  ASSERT_TRUE(readPage(pageReader, txt, renderer, history.currentOffset(), 1, 100, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"second"}));
  EXPECT_EQ(nextOffset, 13u);

  ASSERT_TRUE(history.forward());
  EXPECT_EQ(history.currentPage(), 2u);
  EXPECT_EQ(history.currentOffset(), 13u);
  ASSERT_TRUE(readPage(pageReader, txt, renderer, history.currentOffset(), 1, 100, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"third"}));
  ASSERT_TRUE(history.forward());
  EXPECT_EQ(history.currentPage(), 3u);
  EXPECT_EQ(history.currentOffset(), 19u);
  ASSERT_TRUE(readPage(pageReader, txt, renderer, history.currentOffset(), 1, 100, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"fourth"}));
  EXPECT_FALSE(history.hasForward());
  EXPECT_EQ(history.currentPage(), 3u);
}

TEST(TxtOnDemandTest, HistoryCapacityEvictsOldestOnlyAfterThe257thStart) {
  static_assert(TxtPageHistory::CAPACITY == 256);
  TxtPageHistory history;
  history.reset();

  for (size_t page = 1; page < TxtPageHistory::CAPACITY; ++page) {
    ASSERT_TRUE(history.advance(page * 10));
  }
  EXPECT_EQ(history.currentPage(), 255u);
  EXPECT_EQ(history.currentOffset(), 2550u);
  for (size_t page = 0; page < TxtPageHistory::CAPACITY - 1; ++page) {
    ASSERT_TRUE(history.retreat());
  }
  EXPECT_EQ(history.currentPage(), 0u);
  EXPECT_EQ(history.currentOffset(), 0u);
  EXPECT_FALSE(history.retreat());

  for (size_t page = 1; page < TxtPageHistory::CAPACITY; ++page) {
    ASSERT_TRUE(history.forward());
  }
  ASSERT_TRUE(history.advance(TxtPageHistory::CAPACITY * 10));
  EXPECT_EQ(history.currentPage(), 256u);
  EXPECT_EQ(history.currentOffset(), 2560u);

  for (size_t page = 0; page < TxtPageHistory::CAPACITY - 1; ++page) {
    ASSERT_TRUE(history.retreat());
  }
  EXPECT_EQ(history.currentPage(), 1u);
  EXPECT_EQ(history.currentOffset(), 10u);
  EXPECT_FALSE(history.retreat());
  EXPECT_EQ(history.currentOffset(), 10u);
  ASSERT_TRUE(history.forward());
  EXPECT_EQ(history.currentPage(), 2u);
  EXPECT_EQ(history.currentOffset(), 20u);
}

TEST(TxtOnDemandProgressTest, SavesAndRestoresExactOffsetWithoutScanningFromStart) {
  Storage.clear();
  std::string content;
  content.reserve(96 * 1024);
  for (int i = 0; i < 48 * 1024; ++i) content += "x\n";
  Txt txt(content, "/books/resume.txt", "/.crosspoint/txt_resume");
  TxtProgressFile progress;
  size_t restored = 0;
  EXPECT_FALSE(progress.load(txt, restored));
  constexpr size_t checkpoint = 40000;
  ASSERT_TRUE(progress.save(txt, checkpoint));

  TxtProgressFile reopened;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored, checkpoint);

  txt.reads.clear();
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;
  ASSERT_TRUE(readPage(pageReader, txt, renderer, restored, 1, 100, lines, nextOffset));
  ASSERT_EQ(txt.reads.size(), 1u);
  EXPECT_EQ(txt.reads[0].first, checkpoint);
  EXPECT_EQ(txt.reads[0].second, TxtPageReader::READ_WINDOW_SIZE);
  EXPECT_NE(txt.reads[0].first, 0u);
  EXPECT_LT(txt.reads[0].second, txt.getFileSize());
  EXPECT_EQ(lines, (std::vector<std::string>{"x"}));
}

TEST(TxtOnDemandProgressTest, ReopenRestoresDisplayOrdinalAndThreeExactPreviousStarts) {
  Storage.clear();
  Txt txt("page0\npage1\npage2\npage3\npage4\n" + std::string(300, 'x'), "/books/ordinal.txt",
          "/.crosspoint/txt_ordinal");
  TxtProgressState saved;
  saved.pageStartOffset = 18;
  saved.hasDisplayOrdinal = true;
  saved.displayOrdinal = 183;
  saved.previousPageStarts = {0, 6, 12};
  saved.previousPageCount = 3;

  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, saved));
  const auto& storedRecord = Storage.files["/.crosspoint/txt_ordinal/progress0.bin"];
  EXPECT_EQ(storedRecord.size(), 84u);
  EXPECT_EQ(storedRecord[4], 4u);
  EXPECT_EQ(storedRecord[49], 0u);
  EXPECT_EQ(storedRecord[48], 3u);

  txt.reads.clear();
  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, 18u);
  ASSERT_TRUE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.displayOrdinal, 183u);
  EXPECT_EQ(restored.previousPageCount, 3u);
  EXPECT_EQ(restored.previousPageStarts, (std::array<size_t, 3>{0, 6, 12}));
  // The micro-history and ordinal need no source reads. The exact checkpoint's
  // existing UTF-8 boundary check is still performed once.
  ASSERT_EQ(txt.reads.size(), 1u);
  EXPECT_EQ(txt.reads[0].first, 18u);

  for (int reopenCount = 0; reopenCount < 3; ++reopenCount) {
    TxtProgressFile nextReopen;
    TxtProgressState sameCheckpoint;
    ASSERT_TRUE(nextReopen.load(txt, sameCheckpoint));
    EXPECT_EQ(sameCheckpoint.pageStartOffset, 18u);
    ASSERT_TRUE(sameCheckpoint.hasDisplayOrdinal);
    EXPECT_EQ(sameCheckpoint.displayOrdinal, 183u);
  }

  TxtPageHistory history;
  ASSERT_TRUE(
      history.resetWithPrevious(restored.previousPageStarts, restored.previousPageCount, restored.pageStartOffset));
  size_t ordinal = restored.displayOrdinal;
  TxtPageEstimate estimate;
  ASSERT_TRUE(estimate.observePage(18, 24));
  const uint64_t estimateAtReopen = estimate.bytesPerPage();
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;

  ASSERT_EQ(history.currentOffset(), 18u);
  EXPECT_EQ(ordinal, 183u);
  ASSERT_TRUE(history.retreat());
  --ordinal;
  ASSERT_TRUE(readPage(pageReader, txt, renderer, history.currentOffset(), 1, 100, lines, nextOffset));
  EXPECT_EQ(history.currentOffset(), 12u);
  EXPECT_EQ(lines, (std::vector<std::string>{"page2"}));
  EXPECT_EQ(ordinal, 182u);
  EXPECT_FALSE(estimate.observePage(12, 18));
  EXPECT_EQ(estimate.bytesPerPage(), estimateAtReopen);
  ASSERT_TRUE(history.retreat());
  --ordinal;
  EXPECT_EQ(history.currentOffset(), 6u);
  EXPECT_EQ(ordinal, 181u);
  EXPECT_FALSE(estimate.observePage(6, 12));
  ASSERT_TRUE(history.retreat());
  --ordinal;
  EXPECT_EQ(history.currentOffset(), 0u);
  EXPECT_EQ(ordinal, 180u);
  EXPECT_FALSE(estimate.observePage(0, 6));
  EXPECT_FALSE(history.retreat());

  ASSERT_TRUE(history.forward());
  ++ordinal;
  EXPECT_FALSE(estimate.observePage(0, 6));
  ASSERT_TRUE(history.forward());
  ++ordinal;
  EXPECT_FALSE(estimate.observePage(6, 12));
  ASSERT_TRUE(history.forward());
  ++ordinal;
  EXPECT_FALSE(estimate.observePage(12, 18));
  EXPECT_FALSE(estimate.observePage(18, 24));
  EXPECT_EQ(history.currentOffset(), 18u);
  EXPECT_EQ(ordinal, 183u);
  EXPECT_EQ(estimate.bytesPerPage(), estimateAtReopen);
  ASSERT_TRUE(readPage(pageReader, txt, renderer, history.currentOffset(), 1, 100, lines, nextOffset));
  EXPECT_EQ(lines, (std::vector<std::string>{"page3"}));
  ASSERT_TRUE(history.advance(nextOffset));
  ++ordinal;
  EXPECT_TRUE(estimate.observePage(24, 30));
  EXPECT_EQ(history.currentOffset(), 24u);
  EXPECT_EQ(ordinal, 184u);

  // M is an estimate and can change independently; restoring n uses its saved value.
  TxtPageEstimate coldEstimate;
  EXPECT_EQ(restored.displayOrdinal, 183u);
  EXPECT_NE(coldEstimate.totalPagesFor(txt.getFileSize()),
            coldEstimate.totalPagesFrom(restored.displayOrdinal, txt.getFileSize(), 18, false));
}

TEST(TxtOnDemandProgressTest, MicroHistoryPersistsOneTwoOrThreeStartsInChronologicalOrder) {
  Storage.clear();
  Txt txt("a\nb\nc\nd\ne\n", "/books/micro.txt", "/.crosspoint/txt_micro");
  const size_t starts[] = {0, 2, 4, 6};

  for (size_t previousCount = 1; previousCount <= 3; ++previousCount) {
    TxtProgressState state;
    state.pageStartOffset = starts[previousCount];
    state.hasDisplayOrdinal = true;
    state.displayOrdinal = 20 + previousCount;
    state.previousPageCount = previousCount;
    for (size_t i = 0; i < previousCount; ++i) state.previousPageStarts[i] = starts[i];

    TxtProgressFile progress;
    ASSERT_TRUE(progress.save(txt, state));
    TxtProgressFile reopened;
    TxtProgressState restored;
    ASSERT_TRUE(reopened.load(txt, restored));
    EXPECT_EQ(restored.previousPageCount, previousCount);
    for (size_t i = 0; i < previousCount; ++i) EXPECT_EQ(restored.previousPageStarts[i], starts[i]);
  }
}

TEST(TxtOnDemandProgressTest, SeededHistoryContinuesIntoTheBoundedSessionRing) {
  TxtPageHistory history;
  ASSERT_TRUE(history.resetWithPrevious({0, 100, 200}, 3, 300));
  EXPECT_EQ(history.currentOffset(), 300u);
  EXPECT_EQ(history.currentPage(), 3u);

  size_t nextOffset = 300;
  for (size_t page = 0; page < TxtPageHistory::CAPACITY; ++page) {
    nextOffset += 100;
    ASSERT_TRUE(history.advance(nextOffset));
  }
  EXPECT_EQ(history.currentOffset(), nextOffset);
  for (size_t page = 0; page < TxtPageHistory::CAPACITY - 1; ++page) ASSERT_TRUE(history.retreat());
  EXPECT_EQ(history.currentOffset(), 400u);
  EXPECT_FALSE(history.retreat());

  std::array<size_t, 3> previous{};
  EXPECT_EQ(history.copyPreviousOffsets(previous), 0u);
}

TEST(TxtOnDemandProgressTest, LegacyVersionOneRestoresExactOffsetWithoutOrdinalOrMicroHistory) {
  Storage.clear();
  Txt txt("zero\none\ntwo\n", "/books/legacy.txt", "/.crosspoint/txt_legacy");
  Storage.files["/.crosspoint/txt_legacy/progress0.bin"] = makeLegacyCheckpoint(txt, 1, 5);

  TxtProgressFile reopened;
  TxtProgressState state;
  ASSERT_TRUE(reopened.load(txt, state));
  EXPECT_EQ(state.pageStartOffset, 5u);
  EXPECT_FALSE(state.hasDisplayOrdinal);
  EXPECT_EQ(state.previousPageCount, 0u);
}

TEST(TxtOnDemandProgressTest, VersionTwoKeepsAnchorAndOrdinalButDropsOldLayoutHistory) {
  Storage.clear();
  const std::string content = "\xEA\xB0\x80\xEB\x82\x98.\xEB\x8B\xA4\xEB\x9D\xBC...";
  Txt txt(content, "/books/phase5-v2.txt", "/.crosspoint/txt_phase5_v2");
  Storage.files["/.crosspoint/txt_phase5_v2/progress0.bin"] = makeCheckpointFixture(txt, 2, 4, 6, 2, {0, 0, 0}, 1);

  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, 6u);
  ASSERT_TRUE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.displayOrdinal, 2u);
  EXPECT_EQ(restored.previousPageCount, 0u);
  EXPECT_EQ(restored.previousPageStarts, (std::array<size_t, 3>{0, 0, 0}));
  ASSERT_EQ(txt.reads.size(), 1u);
  EXPECT_EQ(txt.reads[0], (std::pair<size_t, size_t>{6, 1}));

  TxtPageHistory resumedHistory;
  ASSERT_TRUE(resumedHistory.resetWithPrevious(restored.previousPageStarts, restored.previousPageCount,
                                               restored.pageStartOffset));
  EXPECT_FALSE(resumedHistory.retreat());

  // Under the new rule page zero ends at byte 3; the legacy v2 chain would
  // have replayed byte 6 after Back and skipped source bytes 3 through 5.
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t newPageEnd = 0;
  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 4, lines, newPageEnd));
  EXPECT_EQ(lines, (std::vector<std::string>{"\xEA\xB0\x80"}));
  EXPECT_EQ(newPageEnd, 3u);

  const auto v2Path = "/.crosspoint/txt_phase5_v2/progress0.bin";
  ASSERT_TRUE(reopened.save(txt, restored));
  EXPECT_EQ(Storage.files[v2Path][4], 2u);
  const auto& upgradedSameState = Storage.files["/.crosspoint/txt_phase5_v2/progress1.bin"];
  ASSERT_EQ(upgradedSameState.size(), 84u);
  EXPECT_EQ(upgradedSameState[4], 4u);
  EXPECT_EQ(upgradedSameState[49], 0u);

  TxtProgressState changed = restored;
  changed.pageStartOffset = 10;
  changed.displayOrdinal = 3;
  ASSERT_TRUE(reopened.save(txt, changed));
  const auto& upgraded = Storage.files["/.crosspoint/txt_phase5_v2/progress0.bin"];
  ASSERT_EQ(upgraded.size(), 84u);
  EXPECT_EQ(upgraded[4], 4u);

  TxtProgressFile upgradedReopen;
  TxtProgressState upgradedState;
  ASSERT_TRUE(upgradedReopen.load(txt, upgradedState));
  EXPECT_EQ(upgradedState.pageStartOffset, 10u);
  ASSERT_TRUE(upgradedState.hasDisplayOrdinal);
  EXPECT_EQ(upgradedState.displayOrdinal, 3u);
  EXPECT_EQ(upgradedState.previousPageCount, 0u);
}

TEST(TxtOnDemandProgressTest, MixedVersionSlotsSelectByGenerationAndKeepOnlyCurrentLayoutHistory) {
  const std::string cache = "/.crosspoint/txt_phase5_mixed";
  Txt txt("a\nb\nc\nd\ne\n", "/books/phase5-mixed.txt", cache);
  const auto v2 = makeCheckpointFixture(txt, 2, 8, 4, 3, {0, 2, 0}, 2);
  const auto v3 = makeCheckpointFixture(txt, 3, 7, 6, 4, {0, 2, 4}, 3);

  Storage.clear();
  Storage.files[cache + "/progress0.bin"] = v2;
  Storage.files[cache + "/progress1.bin"] = v3;
  TxtProgressFile v2Wins;
  TxtProgressState restored;
  ASSERT_TRUE(v2Wins.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, 4u);
  EXPECT_EQ(restored.displayOrdinal, 3u);
  EXPECT_EQ(restored.previousPageCount, 0u);

  Storage.clear();
  Storage.files[cache + "/progress0.bin"] = makeCheckpointFixture(txt, 3, 8, 6, 4, {0, 2, 4}, 3);
  Storage.files[cache + "/progress1.bin"] = makeCheckpointFixture(txt, 2, 7, 4, 3, {0, 2, 0}, 2);
  TxtProgressFile v3Wins;
  ASSERT_TRUE(v3Wins.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, 6u);
  EXPECT_EQ(restored.displayOrdinal, 4u);
  EXPECT_EQ(restored.previousPageCount, 3u);
  EXPECT_EQ(restored.previousPageStarts, (std::array<size_t, 3>{0, 2, 4}));
}

TEST(TxtOnDemandProgressTest, CjkV4HistoryReopensAndReplaysExactRenderedPages) {
  Storage.clear();
  const std::string content = "\xEA\xB0\x80\xEB\x82\x98.\xEB\x8B\xA4\xEB\x9D\xBC!\xEB\xA7\x88\xEB\xB0\x94";
  const std::vector<std::string> expectedLines = {"\xEA\xB0\x80", "\xEB\x82\x98.", "\xEB\x8B\xA4", "\xEB\x9D\xBC!",
                                                  "\xEB\xA7\x88\xEB\xB0\x94"};
  const std::array<size_t, 5> starts = {0, 3, 7, 10, 14};
  const std::array<size_t, 5> ends = {3, 7, 10, 14, 20};
  Txt txt(content, "/books/phase5-history.txt", "/.crosspoint/txt_phase5_history");
  GfxRenderer renderer;
  TxtPageReader pageReader;
  TxtPageHistory history;
  TxtProgressFile progress;
  history.reset();
  std::vector<std::string> lines;

  for (size_t page = 0; page + 1 < starts.size(); ++page) {
    size_t pageEnd = starts[page];
    ASSERT_EQ(history.currentOffset(), starts[page]);
    ASSERT_TRUE(readPage(pageReader, txt, renderer, starts[page], 1, 4, lines, pageEnd));
    EXPECT_EQ(lines, (std::vector<std::string>{expectedLines[page]}));
    EXPECT_EQ(pageEnd, ends[page]);
    ASSERT_TRUE(history.advance(pageEnd));

    TxtProgressState checkpoint;
    checkpoint.pageStartOffset = history.currentOffset();
    checkpoint.hasDisplayOrdinal = true;
    checkpoint.displayOrdinal = page + 2;
    checkpoint.previousPageCount = history.copyPreviousOffsets(checkpoint.previousPageStarts);
    ASSERT_TRUE(progress.save(txt, checkpoint));
  }

  const auto& latest0 = Storage.files["/.crosspoint/txt_phase5_history/progress0.bin"];
  const auto& latest1 = Storage.files["/.crosspoint/txt_phase5_history/progress1.bin"];
  EXPECT_EQ(latest0[4], 4u);
  EXPECT_EQ(latest1[4], 4u);
  EXPECT_EQ(latest1[48], 3u);

  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, starts.back());
  ASSERT_TRUE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.displayOrdinal, 5u);
  EXPECT_EQ(restored.previousPageCount, 3u);
  EXPECT_EQ(restored.previousPageStarts, (std::array<size_t, 3>{3, 7, 10}));

  TxtPageHistory replay;
  ASSERT_TRUE(
      replay.resetWithPrevious(restored.previousPageStarts, restored.previousPageCount, restored.pageStartOffset));
  for (size_t page = 3; page > 0; --page) {
    ASSERT_TRUE(replay.retreat());
    EXPECT_EQ(replay.currentOffset(), starts[page]);
    size_t pageEnd = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, replay.currentOffset(), 1, 4, lines, pageEnd));
    EXPECT_EQ(lines, (std::vector<std::string>{expectedLines[page]}));
    EXPECT_EQ(pageEnd, starts[page + 1]);
  }
  for (size_t page = 2; page < starts.size(); ++page) {
    ASSERT_TRUE(replay.forward());
    EXPECT_EQ(replay.currentOffset(), starts[page]);
    size_t pageEnd = 0;
    ASSERT_TRUE(readPage(pageReader, txt, renderer, replay.currentOffset(), 1, 4, lines, pageEnd));
    EXPECT_EQ(lines, (std::vector<std::string>{expectedLines[page]}));
    EXPECT_EQ(pageEnd, ends[page]);
  }
}

TEST(TxtOnDemandProgressTest, InvalidOptionalOrdinalDoesNotInvalidateExactCheckpoint) {
  Storage.clear();
  Txt txt("zero\none\ntwo\n", "/books/invalid-ordinal.txt", "/.crosspoint/txt_invalid_ordinal");
  TxtProgressState state;
  state.pageStartOffset = 5;
  state.hasDisplayOrdinal = true;
  state.displayOrdinal = 2;
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, state));

  auto& record = Storage.files["/.crosspoint/txt_invalid_ordinal/progress0.bin"];
  ASSERT_EQ(record.size(), 84u);
  writeU64(record.data() + 40, 0);
  updateCheckpointMetadataChecksum(record);

  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, 5u);
  EXPECT_FALSE(restored.hasDisplayOrdinal);
}

TEST(TxtOnDemandProgressTest, InvalidMicroHistoryIsDiscardedWithoutLosingOrdinalOrCheckpoint) {
  Storage.clear();
  Txt txt("zero\none\ntwo\nthree\n", "/books/invalid-history.txt", "/.crosspoint/txt_invalid_history");
  TxtProgressState state;
  state.pageStartOffset = 9;
  state.hasDisplayOrdinal = true;
  state.displayOrdinal = 3;
  state.previousPageCount = 2;
  state.previousPageStarts = {5, 5, 0};
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, state));

  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, 9u);
  ASSERT_TRUE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.displayOrdinal, 3u);
  EXPECT_EQ(restored.previousPageCount, 0u);
}

TEST(TxtOnDemandProgressTest, CorruptOptionalMetadataDoesNotInvalidateExactCheckpoint) {
  Storage.clear();
  Txt txt("zero\none\ntwo\n", "/books/corrupt-metadata.txt", "/.crosspoint/txt_corrupt_metadata");
  TxtProgressState state;
  state.pageStartOffset = 5;
  state.hasDisplayOrdinal = true;
  state.displayOrdinal = 22;
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, state));
  Storage.files["/.crosspoint/txt_corrupt_metadata/progress0.bin"][40] ^= 0x01;

  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, 5u);
  EXPECT_FALSE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.previousPageCount, 0u);
}

TEST(TxtOnDemandProgressTest, InvalidV3HistoryPreservesAnchorAndOrdinal) {
  Storage.clear();
  const std::string cache = "/.crosspoint/txt_invalid_v3_history";
  Txt txt("zero\none\ntwo\n", "/books/invalid-v3-history.txt", cache);
  Storage.files[cache + "/progress0.bin"] = makeCheckpointFixture(txt, 3, 1, 9, 3, {5, 5, 0}, 2);

  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, 9u);
  ASSERT_TRUE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.displayOrdinal, 3u);
  EXPECT_EQ(restored.previousPageCount, 0u);
  EXPECT_EQ(restored.previousPageStarts, (std::array<size_t, 3>{0, 0, 0}));
}

TEST(TxtOnDemandProgressTest, KoreanModeMismatchKeepsExactAnchorAndDropsPreviousPageMicroHistory) {
  Storage.clear();
  Txt txt("p0\np1\np2\np3\n", "/books/mode.txt", "/.crosspoint/txt_mode");
  TxtProgressState original;
  original.pageStartOffset = 6;
  original.hasDisplayOrdinal = true;
  original.displayOrdinal = 3;
  original.previousPageStarts = {0, 3, 0};
  original.previousPageCount = 2;

  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, original, false));
  const auto& offRecord = Storage.files["/.crosspoint/txt_mode/progress0.bin"];
  ASSERT_EQ(offRecord[4], 4u);
  EXPECT_EQ(offRecord[49], 0u);

  // A same-state mode change is still a semantic upgrade and must write a new
  // v4 slot with the requested Korean wrapping mode.
  ASSERT_TRUE(progress.save(txt, original, true));
  const auto& onRecord = Storage.files["/.crosspoint/txt_mode/progress1.bin"];
  ASSERT_EQ(onRecord[4], 4u);
  EXPECT_EQ(onRecord[49], 1u);

  TxtProgressFile openedOn;
  TxtProgressState restoredOn;
  ASSERT_TRUE(openedOn.load(txt, restoredOn, true));
  EXPECT_EQ(restoredOn.pageStartOffset, original.pageStartOffset);
  ASSERT_TRUE(restoredOn.hasDisplayOrdinal);
  EXPECT_EQ(restoredOn.displayOrdinal, original.displayOrdinal);
  EXPECT_EQ(restoredOn.previousPageCount, 2u);

  // The newest ON slot wins by generation, but its optional page boundaries
  // cannot leak into an OFF session or revive the older OFF slot's history.
  TxtProgressFile openedOff;
  TxtProgressState restoredOff;
  ASSERT_TRUE(openedOff.load(txt, restoredOff, false));
  EXPECT_EQ(restoredOff.pageStartOffset, original.pageStartOffset);
  EXPECT_FALSE(restoredOff.hasDisplayOrdinal);
  EXPECT_EQ(restoredOff.previousPageCount, 0u);

  // This is the activity's local reflow checkpoint: preserve the visible byte
  // anchor and replace stale history with a v4 OFF record.
  TxtProgressState resetAtAnchor;
  resetAtAnchor.pageStartOffset = original.pageStartOffset;
  ASSERT_TRUE(openedOff.save(txt, resetAtAnchor, false));
  const auto& resetRecord = Storage.files["/.crosspoint/txt_mode/progress0.bin"];
  EXPECT_EQ(resetRecord[4], 4u);
  EXPECT_EQ(resetRecord[49], 0u);
  EXPECT_EQ(resetRecord[48], 0u);
  EXPECT_EQ(readU64(resetRecord.data() + 40), 0u);
}

TEST(TxtOnDemandProgressTest, OlderCheckpointWithKoreanModeOnRestoresOnlyExactSourceAnchor) {
  Storage.clear();
  const std::string cache = "/.crosspoint/txt_legacy_on";
  Txt txt("p0\np1\np2\np3\n", "/books/legacy-on.txt", cache);
  Storage.files[cache + "/progress0.bin"] = makeCheckpointFixture(txt, 3, 4, 6, 3, {0, 3, 0}, 2, false);

  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored, true));
  EXPECT_EQ(restored.pageStartOffset, 6u);
  EXPECT_FALSE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.previousPageCount, 0u);

  ASSERT_TRUE(reopened.save(txt, restored, true));
  const auto& upgraded = Storage.files[cache + "/progress1.bin"];
  ASSERT_EQ(upgraded[4], 4u);
  EXPECT_EQ(upgraded[49], 1u);
  EXPECT_EQ(upgraded[48], 0u);
  EXPECT_EQ(readU64(upgraded.data() + 40), 0u);
}

TEST(TxtOnDemandProgressTest, InvalidV4ModeDropsOptionalMetadataButPreservesCoreAnchor) {
  Storage.clear();
  const std::string cache = "/.crosspoint/txt_invalid_mode";
  Txt txt("p0\np1\np2\np3\n", "/books/invalid-mode.txt", cache);
  Storage.files[cache + "/progress0.bin"] = makeCheckpointFixture(txt, 4, 1, 6, 3, {0, 3, 0}, 2, false);
  auto& record = Storage.files[cache + "/progress0.bin"];
  record[49] = 2;
  updateCheckpointMetadataChecksum(record);

  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored, false));
  EXPECT_EQ(restored.pageStartOffset, 6u);
  EXPECT_FALSE(restored.hasDisplayOrdinal);
  EXPECT_EQ(restored.previousPageCount, 0u);
}

TEST(TxtOnDemandProgressTest, FailedOrdinalAndHistoryUpdateLeavesPreviousCheckpointRecoverable) {
  Storage.clear();
  Txt txt("page0\npage1\npage2\npage3\n" + std::string(100, 'x'), "/books/metadata-failure.txt",
          "/.crosspoint/txt_metadata_failure");
  TxtProgressState original;
  original.pageStartOffset = 6;
  original.hasDisplayOrdinal = true;
  original.displayOrdinal = 42;
  original.previousPageStarts = {0, 0, 0};
  original.previousPageCount = 1;
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, original));

  TxtProgressState replacement = original;
  replacement.displayOrdinal = 43;
  replacement.pageStartOffset = 12;
  replacement.previousPageStarts = {0, 6, 0};
  replacement.previousPageCount = 2;
  Storage.failRename = true;
  EXPECT_FALSE(progress.save(txt, replacement));
  Storage.failRename = false;

  TxtProgressFile reopened;
  TxtProgressState restored;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored.pageStartOffset, 6u);
  EXPECT_EQ(restored.displayOrdinal, 42u);
  EXPECT_EQ(restored.previousPageCount, 1u);
  EXPECT_EQ(restored.previousPageStarts[0], 0u);
}

TEST(TxtOnDemandProgressTest, RejectsLegacyOrdinalAndMismatchedBookIdentity) {
  Storage.clear();
  const std::string cache = "/.crosspoint/txt_identity";
  Storage.files[cache + "/progress.bin"] = {50, 0, 0, 0};
  Txt original("first\nsecond\n", "/books/a.txt", cache);
  TxtProgressFile progress;
  size_t restored = 99;
  EXPECT_FALSE(progress.load(original, restored));
  EXPECT_EQ(restored, 0u);
  ASSERT_TRUE(progress.save(original, 6));

  Txt renamed("first\nsecond\n", "/books/renamed.txt", cache);
  TxtProgressFile wrongPath;
  EXPECT_FALSE(wrongPath.load(renamed, restored));
  Txt replaced("first\nsecond\nthird\n", "/books/a.txt", cache);
  TxtProgressFile wrongSize;
  EXPECT_FALSE(wrongSize.load(replaced, restored));
}

TEST(TxtOnDemandProgressTest, CorruptNewestSlotFallsBackToOlderValidCheckpoint) {
  Storage.clear();
  Txt txt("zero\none\ntwo\n", "/books/fallback.txt", "/.crosspoint/txt_fallback");
  TxtProgressFile progress;
  size_t ignored = 0;
  EXPECT_FALSE(progress.load(txt, ignored));
  ASSERT_TRUE(progress.save(txt, 0));
  ASSERT_TRUE(progress.save(txt, 5));
  auto& newest = Storage.files["/.crosspoint/txt_fallback/progress1.bin"];
  ASSERT_EQ(newest.size(), 84u);
  newest.resize(7);

  TxtProgressFile reopened;
  size_t restored = 99;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored, 0u);
}

TEST(TxtOnDemandProgressTest, CorruptV4CoreChecksumFallsBackToOlderValidCheckpoint) {
  Storage.clear();
  const std::string cache = "/.crosspoint/txt_core_checksum";
  Txt txt("zero\none\ntwo\n", "/books/core-checksum.txt", cache);
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, 0));
  ASSERT_TRUE(progress.save(txt, 5));
  auto& newest = Storage.files[cache + "/progress1.bin"];
  ASSERT_EQ(newest.size(), 84u);
  ASSERT_EQ(newest[4], 4u);
  newest[12] ^= 0x01;

  TxtProgressFile reopened;
  size_t restored = 99;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored, 0u);
}

TEST(TxtOnDemandProgressTest, PartialWriteAndRenameFailureKeepLastGoodCheckpoint) {
  Storage.clear();
  Txt txt("first\nsecond\nthird\n", "/books/failure.txt", "/.crosspoint/txt_failure");
  TxtProgressFile progress;
  size_t ignored = 0;
  EXPECT_FALSE(progress.load(txt, ignored));
  ASSERT_TRUE(progress.save(txt, 0));

  Storage.writeLimit = 3;
  EXPECT_FALSE(progress.save(txt, 6));
  Storage.writeLimit = std::numeric_limits<size_t>::max();
  TxtProgressFile afterPartial;
  size_t restored = 99;
  ASSERT_TRUE(afterPartial.load(txt, restored));
  EXPECT_EQ(restored, 0u);

  Storage.failRename = true;
  EXPECT_FALSE(progress.save(txt, 6));
  Storage.failRename = false;
  TxtProgressFile afterRenameFailure;
  ASSERT_TRUE(afterRenameFailure.load(txt, restored));
  EXPECT_EQ(restored, 0u);
}

TEST(TxtOnDemandProgressTest, ReadbackFailureKeepsTheActiveV4Checkpoint) {
  Storage.clear();
  const std::string cache = "/.crosspoint/txt_readback_failure";
  Txt txt("first\nsecond\nthird\n", "/books/readback-failure.txt", cache);
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, 0));
  const auto original = Storage.files[cache + "/progress0.bin"];
  ASSERT_EQ(original.size(), 84u);
  EXPECT_EQ(original[4], 4u);

  Storage.failNextReadPath = cache + "/progress1.bin.tmp";
  EXPECT_FALSE(progress.save(txt, 6));
  EXPECT_EQ(Storage.files[cache + "/progress0.bin"], original);

  TxtProgressFile reopened;
  size_t restored = 99;
  ASSERT_TRUE(reopened.load(txt, restored));
  EXPECT_EQ(restored, 0u);
}

TEST(TxtOnDemandProgressTest, ReadErrorDoesNotReplaceOnlyCheckpointWhenRenameFails) {
  Storage.clear();
  Txt txt("first\nsecond\nthird\n", "/books/read-error.txt", "/.crosspoint/txt_read_error");
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, 0));

  const std::string onlyCheckpoint = "/.crosspoint/txt_read_error/progress0.bin";
  ASSERT_TRUE(Storage.exists(onlyCheckpoint.c_str()));
  Storage.failNextReadPath = onlyCheckpoint;
  Storage.failRename = true;

  TxtProgressFile resumed;
  EXPECT_FALSE(resumed.save(txt, 6));
  Storage.failRename = false;

  EXPECT_TRUE(Storage.exists(onlyCheckpoint.c_str()));
  TxtProgressFile afterReadError;
  size_t restored = 99;
  ASSERT_TRUE(afterReadError.load(txt, restored));
  EXPECT_EQ(restored, 0u);
}

TEST(TxtOnDemandProgressTest, SourceBoundaryReadErrorDoesNotReplaceOnlyCheckpointWhenRenameFails) {
  Storage.clear();
  Txt txt("first\nsecond\nthird\n", "/books/source-read-error.txt", "/.crosspoint/txt_source_read_error");
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, 6));

  const std::string onlyCheckpoint = "/.crosspoint/txt_source_read_error/progress0.bin";
  ASSERT_TRUE(Storage.exists(onlyCheckpoint.c_str()));
  txt.failNextRead = true;
  Storage.failRename = true;

  TxtProgressFile resumed;
  EXPECT_FALSE(resumed.save(txt, 12));
  Storage.failRename = false;

  EXPECT_TRUE(Storage.exists(onlyCheckpoint.c_str()));
  TxtProgressFile afterReadError;
  size_t restored = 99;
  ASSERT_TRUE(afterReadError.load(txt, restored));
  EXPECT_EQ(restored, 6u);
}

TEST(TxtOnDemandProgressTest, AmbiguousMissingSlotDoesNotDeleteOnlyCheckpointWhenRenameFails) {
  Storage.clear();
  Txt txt("first\nsecond\nthird\n", "/books/exists-error.txt", "/.crosspoint/txt_exists_error");
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, 0));

  const std::string onlyCheckpoint = "/.crosspoint/txt_exists_error/progress0.bin";
  ASSERT_TRUE(Storage.exists(onlyCheckpoint.c_str()));
  Storage.failNextExistsPath = onlyCheckpoint;
  Storage.failRename = true;

  TxtProgressFile resumed;
  EXPECT_FALSE(resumed.save(txt, 6));
  Storage.failRename = false;

  EXPECT_TRUE(Storage.exists(onlyCheckpoint.c_str()));
  TxtProgressFile afterReadError;
  size_t restored = 99;
  ASSERT_TRUE(afterReadError.load(txt, restored));
  EXPECT_EQ(restored, 0u);
}

TEST(TxtOnDemandProgressTest, RejectsUnsafeUtf8OffsetsAndHandlesEmptyAndOnePage) {
  Storage.clear();
  Txt utf8("\xEA\xB0\x80\n", "/books/utf8.txt", "/.crosspoint/txt_utf8");
  TxtProgressFile progress;
  size_t restored = 0;
  EXPECT_FALSE(progress.load(utf8, restored));
  EXPECT_FALSE(progress.save(utf8, 1));
  EXPECT_FALSE(progress.save(utf8, utf8.getFileSize() + 1));
  ASSERT_TRUE(progress.save(utf8, 3));
  TxtProgressFile reopened;
  ASSERT_TRUE(reopened.load(utf8, restored));
  EXPECT_EQ(restored, 3u);

  Storage.clear();
  Txt empty("", "/books/empty.txt", "/.crosspoint/txt_empty");
  TxtProgressFile emptyProgress;
  EXPECT_FALSE(emptyProgress.load(empty, restored));
  ASSERT_TRUE(emptyProgress.save(empty, 0));
  TxtProgressFile emptyReopened;
  ASSERT_TRUE(emptyReopened.load(empty, restored));
  EXPECT_EQ(restored, 0u);

  Storage.clear();
  Txt onePage("short", "/books/one.txt", "/.crosspoint/txt_one");
  TxtProgressFile oneProgress;
  ASSERT_TRUE(oneProgress.save(onePage, 0));
  TxtProgressFile oneReopened;
  ASSERT_TRUE(oneReopened.load(onePage, restored));
  EXPECT_EQ(restored, 0u);
}

TEST(TxtOnDemandProgressTest, RoundTripsLineEndingBlankAndWindowBoundaryStarts) {
  Storage.clear();
  std::string content = "alpha\nbeta\r\n\n";
  content += std::string(TxtPageReader::READ_WINDOW_SIZE + 8, 'z');
  content += "\r\nfinal";
  Txt txt(content, "/books/boundaries.txt", "/.crosspoint/txt_boundaries");
  TxtProgressFile progress;
  size_t restored = 0;

  const size_t starts[] = {0, 6, 12, 13, 13 + TxtPageReader::READ_WINDOW_SIZE};
  for (const size_t start : starts) {
    ASSERT_TRUE(progress.save(txt, start));
    TxtProgressFile reopened;
    ASSERT_TRUE(reopened.load(txt, restored));
    EXPECT_EQ(restored, start);
  }
  EXPECT_GT(restored, TxtPageReader::READ_WINDOW_SIZE);
}

TEST(TxtOnDemandProgressTest, SessionHistoryCanStartFromRestoredOffset) {
  TxtPageHistory history;
  history.reset(500);
  EXPECT_EQ(history.currentOffset(), 500u);
  ASSERT_TRUE(history.advance(600));
  EXPECT_EQ(history.currentOffset(), 600u);
  ASSERT_TRUE(history.retreat());
  EXPECT_EQ(history.currentOffset(), 500u);
}

TEST(TxtOnDemandProgressTest, ReopenedCheckpointStartsFreshSessionHistory) {
  Storage.clear();
  Txt txt("first\nsecond\nthird\n", "/books/reopen-history.txt", "/.crosspoint/txt_reopen_history");
  TxtProgressFile progress;
  ASSERT_TRUE(progress.save(txt, 6));

  TxtProgressFile reopened;
  size_t restoredOffset = 0;
  ASSERT_TRUE(reopened.load(txt, restoredOffset));
  EXPECT_EQ(restoredOffset, 6u);

  TxtPageHistory history;
  history.reset(restoredOffset);
  EXPECT_EQ(history.currentPage(), 0u);
  EXPECT_EQ(history.currentOffset(), restoredOffset);
  EXPECT_FALSE(history.hasForward());
  EXPECT_FALSE(history.retreat());
}

TEST(TxtOnDemandProgressTest, ResetStartsFreshHistoryAtTheCurrentVisibleOffset) {
  TxtPageHistory history;
  history.reset(100);
  ASSERT_TRUE(history.advance(200));
  ASSERT_TRUE(history.advance(300));
  ASSERT_TRUE(history.retreat());
  EXPECT_EQ(history.currentOffset(), 200u);

  history.reset(history.currentOffset());
  EXPECT_EQ(history.currentPage(), 0u);
  EXPECT_EQ(history.currentOffset(), 200u);
  EXPECT_FALSE(history.hasForward());
  EXPECT_FALSE(history.retreat());
}

TEST(TxtPageEstimateTest, HandlesEmptyOnePageSmallAndLargeFiles) {
  TxtPageEstimate estimate;
  EXPECT_EQ(estimate.bytesPerPage(), 2048u);
  EXPECT_EQ(estimate.pageNumberAt(0), 1u);
  EXPECT_EQ(estimate.totalPagesFor(0), 0u);
  EXPECT_EQ(estimate.totalPagesFor(1), 1u);

  ASSERT_TRUE(estimate.observePage(0, 1));
  EXPECT_EQ(estimate.bytesPerPage(), 1920u);
  EXPECT_EQ(estimate.pageNumberAt(0), 1u);
  EXPECT_EQ(estimate.totalPagesFor(1), 1u);
  EXPECT_EQ(estimate.totalPagesFor(3000), 2u);

  estimate.reset();
  ASSERT_TRUE(estimate.observePage(0, 100));
  EXPECT_EQ(estimate.totalPagesFor(100), 1u);
  EXPECT_EQ(estimate.totalPagesFor(3000), 2u);
  EXPECT_EQ(estimate.totalPagesFrom(5, 1000, 0, false), 6u);
  EXPECT_EQ(estimate.totalPagesFrom(5, 5000, 0, false), 7u);

  estimate.reset();
  ASSERT_TRUE(estimate.observePage(0, 2000));
  EXPECT_EQ(estimate.bytesPerPage(), 2042u);
  EXPECT_EQ(estimate.totalPagesFor(1000000), 490u);
  EXPECT_EQ(estimate.pageNumberAt(800000), 392u);
}

TEST(TxtPageEstimateTest, ResumeAtDeepOffsetGetsAnOrdinalWithoutPrefixReads) {
  std::string content;
  content.reserve(40000);
  for (size_t line = 0; line < 400; ++line) {
    content.append(99, 'x');
    content.push_back('\n');
  }
  Txt txt(content);
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;
  constexpr size_t DEEP_OFFSET = 20000;
  ASSERT_TRUE(readPage(pageReader, txt, renderer, DEEP_OFFSET, 20, 100, lines, nextOffset));
  ASSERT_EQ(txt.reads.size(), 1u);
  EXPECT_EQ(txt.reads[0].first, DEEP_OFFSET);
  EXPECT_EQ(txt.reads[0].second, TxtPageReader::READ_WINDOW_SIZE);
  EXPECT_LT(nextOffset, txt.getFileSize());

  TxtPageEstimate estimate;
  const size_t readsAfterRender = txt.reads.size();
  ASSERT_TRUE(estimate.observePage(DEEP_OFFSET, nextOffset));
  EXPECT_EQ(txt.reads.size(), readsAfterRender);
  EXPECT_GT(estimate.pageNumberAt(DEEP_OFFSET), 1u);
  EXPECT_EQ(estimate.bytesPerPage(), 2042u);
  EXPECT_EQ(estimate.pageNumberAt(DEEP_OFFSET), 10u);
  EXPECT_GT(estimate.totalPagesFor(txt.getFileSize()), estimate.pageNumberAt(DEEP_OFFSET));
}

TEST(TxtPageEstimateTest, EstimateOnlyWorkDoesNotReadSourceAndUpdatesInConstantWork) {
  TxtPageEstimate estimate;
  ASSERT_TRUE(estimate.observePage(0, 100));
  Txt txt("source");
  const size_t readsBefore = txt.reads.size();

  // The implementation updates fixed scalar state with no loop, allocation,
  // or Txt dependency. Exercise a long sequence to catch accidental scans.
  constexpr size_t PAGE_COUNT = 100000;
  for (size_t page = 1; page <= PAGE_COUNT; ++page) {
    const size_t start = page * 100;
    ASSERT_TRUE(estimate.observePage(start, start + 100));
  }

  EXPECT_EQ(estimate.bytesPerPage(), 100u);
  EXPECT_EQ(estimate.totalPagesFor(PAGE_COUNT * 100), PAGE_COUNT);
  EXPECT_EQ(txt.reads.size(), readsBefore);
}

TEST(TxtPageEstimateTest, ShortAndLongPageSamplesHaveBoundedInfluence) {
  TxtPageEstimate estimate;
  ASSERT_TRUE(estimate.observePage(0, 100000));
  EXPECT_EQ(estimate.bytesPerPage(), 2304u);

  ASSERT_TRUE(estimate.observePage(100000, 100001));
  EXPECT_EQ(estimate.bytesPerPage(), 2160u);
}

TEST(TxtPageEstimateTest, ConvergesTowardRepeatedNewPageLengths) {
  TxtPageEstimate estimate;
  ASSERT_TRUE(estimate.observePage(0, 100));
  size_t offset = 100;
  for (size_t page = 0; page < 64; ++page) {
    ASSERT_TRUE(estimate.observePage(offset, offset + 1000));
    offset += 1000;
  }
  EXPECT_NEAR(static_cast<double>(estimate.bytesPerPage()), 1000.0, 5.0);
}

TEST(TxtPageEstimateTest, DeepResumeTotalIncludesRemainingPagesAfterAverageGrowsAndReplayIsStable) {
  constexpr size_t FILE_SIZE = 5000000;
  constexpr size_t DEEP_OFFSET = 1000000;
  TxtPageHistory history;
  history.reset(DEEP_OFFSET);
  TxtPageEstimate estimate;

  size_t visiblePageEnd = DEEP_OFFSET + 1000;
  ASSERT_TRUE(estimate.observePage(DEEP_OFFSET, visiblePageEnd));
  const size_t basePageNumber = estimate.pageNumberAt(DEEP_OFFSET);
  EXPECT_GT(basePageNumber, 1u);

  for (size_t page = 0; page < 20; ++page) {
    ASSERT_TRUE(history.advance(visiblePageEnd));
    const size_t pageStart = history.currentOffset();
    ASSERT_EQ(pageStart, visiblePageEnd);
    visiblePageEnd = pageStart + 20000;
    ASSERT_TRUE(estimate.observePage(pageStart, visiblePageEnd));
  }

  const size_t currentPageNumber = basePageNumber + history.currentPage();
  ASSERT_LT(estimate.totalPagesFor(FILE_SIZE), currentPageNumber);
  const size_t totalBeforeReplay = estimate.totalPagesFrom(currentPageNumber, FILE_SIZE, visiblePageEnd, true);
  EXPECT_GT(totalBeforeReplay, currentPageNumber);
  EXPECT_EQ(totalBeforeReplay, currentPageNumber + estimate.totalPagesFor(FILE_SIZE - visiblePageEnd));
  EXPECT_EQ(estimate.totalPagesFrom(currentPageNumber, FILE_SIZE, history.currentOffset(), false),
            currentPageNumber + estimate.totalPagesFor(FILE_SIZE - history.currentOffset()) - 1);
  EXPECT_EQ(estimate.totalPagesFrom(std::numeric_limits<size_t>::max() - 1, FILE_SIZE, visiblePageEnd, true),
            std::numeric_limits<size_t>::max());

  const uint64_t averageAtFrontier = estimate.bytesPerPage();
  const size_t currentPageStart = history.currentOffset();
  ASSERT_TRUE(history.retreat());
  const size_t backPageStart = history.currentOffset();
  EXPECT_FALSE(estimate.observePage(backPageStart, currentPageStart));
  EXPECT_EQ(estimate.bytesPerPage(), averageAtFrontier);

  ASSERT_TRUE(history.forward());
  EXPECT_EQ(history.currentOffset(), currentPageStart);
  EXPECT_FALSE(estimate.observePage(currentPageStart, visiblePageEnd));
  EXPECT_EQ(estimate.bytesPerPage(), averageAtFrontier);
  EXPECT_EQ(estimate.totalPagesFrom(basePageNumber + history.currentPage(), FILE_SIZE, visiblePageEnd, true),
            totalBeforeReplay);
}

TEST(TxtPageEstimateTest, BackReplayAndRingEvictionDoNotDoubleCountPages) {
  TxtPageEstimate estimate;
  TxtPageHistory history;
  history.reset();
  ASSERT_TRUE(estimate.observePage(0, 100));

  for (size_t page = 1; page <= TxtPageHistory::CAPACITY + 44; ++page) {
    const size_t start = history.currentOffset() + 100;
    ASSERT_TRUE(history.advance(start));
    ASSERT_TRUE(estimate.observePage(start, start + 100));
  }
  EXPECT_EQ(history.currentPage(), TxtPageHistory::CAPACITY + 44);
  EXPECT_EQ(estimate.bytesPerPage(), 100u);

  const size_t averageAtFrontier = estimate.bytesPerPage();
  for (size_t page = 0; page < TxtPageHistory::CAPACITY - 1; ++page) {
    ASSERT_TRUE(history.retreat());
    const size_t start = history.currentOffset();
    EXPECT_FALSE(estimate.observePage(start, start + 10000));
  }
  while (history.hasForward()) {
    ASSERT_TRUE(history.forward());
    const size_t start = history.currentOffset();
    EXPECT_FALSE(estimate.observePage(start, start + 10000));
  }
  EXPECT_EQ(estimate.bytesPerPage(), averageAtFrontier);

  const size_t nextStart = history.currentOffset() + 100;
  ASSERT_TRUE(history.advance(nextStart));
  ASSERT_TRUE(estimate.observePage(nextStart, nextStart + 1000));
  const size_t afterNewPage = estimate.bytesPerPage();
  ASSERT_TRUE(history.retreat());
  ASSERT_TRUE(history.forward());
  EXPECT_FALSE(estimate.observePage(nextStart, nextStart + 100000));
  EXPECT_EQ(estimate.bytesPerPage(), afterNewPage);
}

TEST(TxtPageEstimateTest, Utf8PageLengthsUseSourceBytes) {
  const std::string glyph = "\xEA\xB0\x80";
  std::string utf8;
  for (size_t page = 0; page < 128; ++page) utf8 += glyph;
  Txt txt(utf8);
  GfxRenderer renderer;
  TxtPageReader pageReader;
  std::vector<std::string> lines;
  size_t nextOffset = 0;
  ASSERT_TRUE(readPage(pageReader, txt, renderer, 0, 1, 2, lines, nextOffset));
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].size(), 3u);
  EXPECT_EQ(nextOffset, 3u);

  TxtPageEstimate estimate;
  ASSERT_TRUE(estimate.observePage(0, nextOffset));
  EXPECT_EQ(estimate.bytesPerPage(), 1920u);

  size_t pageStart = nextOffset;
  while (pageStart < txt.getFileSize()) {
    const size_t readsBeforePage = txt.reads.size();
    ASSERT_TRUE(readPage(pageReader, txt, renderer, pageStart, 1, 2, lines, nextOffset));
    ASSERT_EQ(txt.reads.size(), readsBeforePage + 1);
    EXPECT_EQ(txt.reads.back().first, pageStart);
    EXPECT_EQ(nextOffset - pageStart, 3u);

    const size_t readsAfterPage = txt.reads.size();
    ASSERT_TRUE(estimate.observePage(pageStart, nextOffset));
    EXPECT_EQ(txt.reads.size(), readsAfterPage);
    pageStart = nextOffset;
  }

  EXPECT_EQ(estimate.bytesPerPage(), 3u);
  EXPECT_EQ(estimate.totalPagesFor(txt.getFileSize()), 128u);
}
