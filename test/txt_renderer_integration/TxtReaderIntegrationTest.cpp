#include <GfxRenderer.h>
#include <FontCacheManager.h>
#include <FontPsram.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <tuple>
#include "util/BookProgress.h"
#include "activities/reader/TxtReaderActivity.h"
#include "FontFixture.h"

size_t readerTestFailNextNothrowBytes = 0;
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
  if (bytes == readerTestFailNextNothrowBytes && bytes != 0) { readerTestFailNextNothrowBytes=0; return nullptr; }
  return std::malloc(bytes);
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
  if (bytes == readerTestFailNextNothrowBytes && bytes != 0) { readerTestFailNextNothrowBytes=0; return nullptr; }
  return std::malloc(bytes);
}

struct ReaderGlyphPosition { uint32_t codepoint; int x; };
struct ReaderTrace {
  std::vector<ReaderGlyphPosition> glyphs;
  std::vector<int> lineAdvances;
  std::vector<size_t> lineGlyphCounts;
  size_t currentLineGlyphCount = 0;
};

struct TxtReaderTestAccess {
  static bool open(TxtReaderActivity& r) {
    if (!r.loadBook()) return false;
    r.cachedFontId=42; r.cachedFontPointSize=12; r.viewportWidth=15; r.cachedViewportHeight=20;
    r.lineAdvance=10; r.linesPerPage=2; r.cachedParagraphAlignment=SETTINGS.paragraphAlignment;
    r.cachedHyphenationEnabled=SETTINGS.hyphenationEnabled;
    r.paragraphGap=SETTINGS.extraParagraphSpacing ? 5 : 0; r.paragraphIndentWidth=SETTINGS.paragraphIndentSpaces*5;
    r.metricContext=std::make_unique<SdCardFont::TxtMetricContext>(); r.bodyTypography.metrics=r.metricContext.get();
    if (r.cachedHyphenationEnabled) {
      r.hyphenationWorkspace=makeUniqueNoThrow<LiangWorkspace>();
      if (!r.hyphenationWorkspace) return false;
    }
    if (r.renderer.getSdCardFonts().at(42)->openTxtMetrics(*r.metricContext) != SdCardFont::TxtMetricStatus::Ok) return false;
    return true;
  }
  static bool loadBook(TxtReaderActivity& r) { return r.loadBook(); }
  static bool initialize(TxtReaderActivity& r) { return r.initializeReader(r.renderer); }
  static bool build(TxtReaderActivity& r) { return r.buildPageIndex(r.renderer); }
  static bool save(TxtReaderActivity& r) { return r.savePageIndexCache(); }
  static bool load(TxtReaderActivity& r) { return r.loadPageIndexCache(); }
  static std::string cache(TxtReaderActivity& r) { return r.txt->getCachePath(); }
  static std::vector<size_t> offsets(TxtReaderActivity& r) { return {r.pageOffsets.begin(),r.pageOffsets.end()}; }
  static bool page(TxtReaderActivity& r,size_t start,std::vector<txt::PageLine>& lines,size_t& next) {
    return r.loadPageAtOffset(r.renderer,start,lines,next);
  }
  static bool renderLines(TxtReaderActivity& r, const std::vector<txt::PageLine>& lines) {
    r.currentPageLines = lines;
    return r.renderPage(r.renderer);
  }
  static std::vector<txt::PageLine> currentLines(const TxtReaderActivity& r) { return r.currentPageLines; }
  static void alignment(TxtReaderActivity& r, uint8_t value) { r.cachedParagraphAlignment = value; }
  static GfxRenderer::TextResult measure(TxtReaderActivity& r, const char* text) {
    return r.renderer.measureTextWithTypography(r.cachedFontId, text, r.bodyTypography);
  }
  static void render(TxtReaderActivity& r,int page=0) { r.initialized=true; r.currentPage=page; r.renderBook(); }
  static bool renderAndCheck(TxtReaderActivity& r, int page) {
    r.currentPage = page;
    r.renderBook();
    return r.lastTextResult.ok();
  }
  static bool primeTextMetrics(TxtReaderActivity& r, const char* text) {
    return r.renderer.measureTextWithTypography(r.cachedFontId, text, r.bodyTypography).ok();
  }
  static void clearMetrics(TxtReaderActivity& r) { r.metricContext->clearCache(); }
  static void geometry(TxtReaderActivity& r,int width,int lines) { r.viewportWidth=width; r.linesPerPage=lines; r.cachedViewportHeight=lines*r.lineAdvance; }
  static bool progress(TxtReaderActivity& r,txt::ProgressRecord& p) { return r.readSavedProgress(p); }
  static void saveProgress(TxtReaderActivity& r) { r.saveProgress(); }
  static int indent(TxtReaderActivity& r) { return r.paragraphIndentWidth; }
  static bool legacyAnchor(TxtReaderActivity& r, uint16_t page, uint32_t& anchor) {
    return r.deriveLegacySourceAnchor(page, anchor);
  }
  static int currentPage(const TxtReaderActivity& r) { return r.currentPage; }
  static void setCurrentPage(TxtReaderActivity& r, int page) { r.currentPage = page; }
  static auto settingsSnapshot(const TxtReaderActivity& r) { return r.captureLayoutSettings(); }
  static bool settingsChanged(const TxtReaderActivity& r, const decltype(r.captureLayoutSettings())& previous) {
    return r.layoutSettingsChanged(previous);
  }
  static void settingsReturned(TxtReaderActivity& r, const decltype(r.captureLayoutSettings())& previous,
                               size_t anchor, bool hasAnchor) {
    r.handleTextSettingsReturn(previous, anchor, hasAnchor);
  }
  static bool initialized(const TxtReaderActivity& r) { return r.initialized; }
  static bool hasPendingAnchor(const TxtReaderActivity& r) { return r.hasPendingSourceAnchor; }
};

void collectReaderTrace(void* context, uint32_t codepoint, int x, int) {
  auto& trace = *static_cast<ReaderTrace*>(context);
  if (codepoint == 0) {
    trace.lineAdvances.push_back(x);
    trace.lineGlyphCounts.push_back(trace.currentLineGlyphCount);
    trace.currentLineGlyphCount = 0;
  } else {
    trace.glyphs.push_back({codepoint, x});
    ++trace.currentLineGlyphCount;
  }
}

bool sameLine(const txt::PageLine& lhs, const txt::PageLine& rhs) {
  return lhs.text == rhs.text && lhs.sourceStart == rhs.sourceStart && lhs.sourceEnd == rhs.sourceEnd &&
         lhs.paragraphStart == rhs.paragraphStart && lhs.paragraphEnd == rhs.paragraphEnd;
}

class TxtReaderIntegration : public ::testing::Test {
 protected:
  SdCardFont font;
  SdCardFont reloadedFont;
  HalDisplay display;
  GfxRenderer renderer{display};
  std::map<int,SdCardFont*> sdFonts;
  std::map<int,TtfEpdFont*> ttfFonts;
  std::unique_ptr<FontCacheManager> manager;
  MappedInputManager input;
  std::unique_ptr<TxtReaderActivity> reader;
  void SetUp() override {
    readerTestFiles.clear(); readerTestShortReadPath.clear(); readerTestShortWritePath.clear();
    readerTestShortReadAfter=0; readerTestPathReads.clear(); readerTestRenameFailure=false; readerTestRenameFailureDestination.clear(); readerTestProgressWrites=0;
    sdFontTestReadFailure=sdFontTestOpenFailure=sdFontTestHandleFailure=false;
    sdFontTestFailArrayAllocations=false; readerTestFailNextNothrowBytes=0; testSettings=CrossPointSettings{};
    makeTypographyFont(); ASSERT_TRUE(font.load("typography.cpfont"));
    renderer.begin(); renderer.insertFont(42,EpdFontFamily(font.getEpdFont())); renderer.registerSdCardFont(42,&font);
    sdFonts.emplace(42,&font); manager=std::make_unique<FontCacheManager>(renderer.getFontMap(),sdFonts,ttfFonts);
    renderer.setFontCacheManager(manager.get());
  }
  void TearDown() override {
    readerTestFailNextNothrowBytes=0; sdFontTestFailArrayAllocations=false;
    sdFontTestReadFailure=sdFontTestOpenFailure=sdFontTestHandleFailure=false;
    readerTestShortReadPath.clear(); readerTestShortWritePath.clear(); readerTestRenameFailure=false; readerTestRenameFailureDestination.clear();
  }
  void open(const std::string& text) {
    readerTestFiles["/book.txt"]={text.begin(),text.end()};
    reader=std::make_unique<TxtReaderActivity>(renderer,input,"/book.txt",false);
    ASSERT_TRUE(TxtReaderTestAccess::open(*reader));
  }
  void installAsciiHyphenationFont(bool replacement, bool includeHyphen) {
    makeTypographyFont(replacement, 0, true, 1, true, true, includeHyphen);
    ASSERT_TRUE(reloadedFont.load("typography-hyphenation.cpfont"));
    renderer.insertFont(42, EpdFontFamily(reloadedFont.getEpdFont()));
    renderer.registerSdCardFont(42, &reloadedFont);
    sdFonts[42] = &reloadedFont;
    manager = std::make_unique<FontCacheManager>(renderer.getFontMap(), sdFonts, ttfFonts);
    renderer.setFontCacheManager(manager.get());
  }
  void checkPartitions(const std::vector<size_t>& offsets,size_t length) {
    size_t owned=0;
    for(size_t p=0;p<offsets.size();++p) {
      std::vector<txt::PageLine> lines; size_t next=0;
      ASSERT_TRUE(TxtReaderTestAccess::page(*reader,offsets[p],lines,next));
      ASSERT_EQ(offsets[p],owned);
      for(const auto& line:lines) { EXPECT_EQ(line.sourceStart,owned); EXPECT_GT(line.sourceEnd,line.sourceStart); owned=line.sourceEnd; }
      ASSERT_EQ(next,owned); ASSERT_EQ(next,p+1<offsets.size()?offsets[p+1]:length);
    }
    EXPECT_EQ(length,owned);
  }
  ReaderTrace drawLines(const std::vector<txt::PageLine>& lines, uint8_t alignment) {
    TxtReaderTestAccess::alignment(*reader, alignment);
    ReaderTrace trace;
    renderer.textTraceContext = &trace;
    renderer.textTrace = collectReaderTrace;
    const bool rendered = TxtReaderTestAccess::renderLines(*reader, lines);
    EXPECT_TRUE(rendered);
    renderer.textTrace = nullptr;
    renderer.textTraceContext = nullptr;
    return trace;
  }
};

TEST_F(TxtReaderIntegration, V9CacheIdentityRejectsV8AndPreservesLegacyProgressConsumers) {
  std::string text;
  text.reserve(1200);
  for (int i = 0; i < 120; ++i) text += "AVAVAVAV\n";
  open(text);
  ASSERT_TRUE(TxtReaderTestAccess::build(*reader));
  const auto oldOffsets = TxtReaderTestAccess::offsets(*reader);
  ASSERT_GT(oldOffsets.size(), 2u);
  ASSERT_TRUE(TxtReaderTestAccess::save(*reader));
  const auto cachePath = TxtReaderTestAccess::cache(*reader);
  const auto currentIndex = readerTestFiles[cachePath + "/index.bin"];
  ASSERT_EQ(txt::INDEX_HEADER_V9_SIZE + oldOffsets.size() * sizeof(uint32_t), currentIndex.size());
  EXPECT_EQ(txt::INDEX_VERSION_V9, currentIndex[4]);
  EXPECT_EQ(txt::INDEX_HEADER_V9_SIZE, txt::INDEX_HEADER_CURRENT_SIZE);

  txt::IndexHeader currentHeader;
  ASSERT_TRUE(txt::parseIndexHeader(currentIndex.data(), txt::INDEX_HEADER_CURRENT_SIZE, currentIndex.size(),
                                    currentHeader));
  EXPECT_EQ(txt::INDEX_VERSION_V9, currentHeader.version);
  EXPECT_EQ(oldOffsets.size(), currentHeader.pageCount);
  EXPECT_EQ(0, currentHeader.hyphenationEnabled);
  readerTestFiles[cachePath + "/progress.bin"] = {1, 0, 0, 0};
  EXPECT_EQ(static_cast<int>(std::min<size_t>(100, 200ULL / oldOffsets.size())), loadBookProgress("/book.txt"));

  for (const uint8_t legacyVersion : {txt::INDEX_VERSION_V6, txt::INDEX_VERSION_V7, txt::INDEX_VERSION_V8}) {
    SCOPED_TRACE(legacyVersion);
    auto legacyIndex = currentIndex;
    legacyIndex.erase(legacyIndex.begin() + txt::INDEX_HEADER_V8_SIZE);
    legacyIndex[4] = legacyVersion;
    readerTestFiles[cachePath + "/index.bin"] = std::move(legacyIndex);

    const uint16_t legacyPage = 1;
    readerTestFiles[cachePath + "/progress.bin"] = {static_cast<uint8_t>(legacyPage), 0, 0, 0};

    uint32_t sourceAnchor = 0;
    ASSERT_TRUE(TxtReaderTestAccess::legacyAnchor(*reader, legacyPage, sourceAnchor));
    EXPECT_EQ(oldOffsets[legacyPage], sourceAnchor);
    EXPECT_FALSE(TxtReaderTestAccess::load(*reader));
    EXPECT_EQ(static_cast<int>(std::min<size_t>(100, (legacyPage + 1) * 100ULL / oldOffsets.size())),
              loadBookProgress("/book.txt"));

    open(text);
    ASSERT_TRUE(TxtReaderTestAccess::initialize(*reader));
    const auto rebuiltOffsets = TxtReaderTestAccess::offsets(*reader);
    EXPECT_EQ(static_cast<int>(txt::PageLayout::pageForSourceAnchor(rebuiltOffsets, sourceAnchor)),
              TxtReaderTestAccess::currentPage(*reader));
    const auto& rebuiltIndex = readerTestFiles[cachePath + "/index.bin"];
    ASSERT_GE(rebuiltIndex.size(), txt::INDEX_HEADER_CURRENT_SIZE);
    EXPECT_EQ(txt::INDEX_VERSION_V9, rebuiltIndex[4]);
  }
}

TEST_F(TxtReaderIntegration, EnglishPatternsDriveTheSameMeasuredAndRenderedTxtLine) {
  installAsciiHyphenationFont(true, true);
  SETTINGS.hyphenationEnabled = true;
  open("extraordinary");
  TxtReaderTestAccess::geometry(*reader, 48, 1);

  std::vector<txt::PageLine> lines;
  size_t next = 0;
  ASSERT_TRUE(TxtReaderTestAccess::page(*reader, 0, lines, next));
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ("extra-", lines[0].text);
  EXPECT_EQ(5u, lines[0].sourceEnd);
  EXPECT_EQ(5u, next);

  ASSERT_TRUE(TxtReaderTestAccess::build(*reader));
  ASSERT_TRUE(TxtReaderTestAccess::save(*reader));
  std::vector<uint32_t> drawnCodepoints;
  renderer.textTraceContext = &drawnCodepoints;
  renderer.textTrace = [](void* context, uint32_t codepoint, int, int) {
    if (codepoint) static_cast<std::vector<uint32_t>*>(context)->push_back(codepoint);
  };
  TxtReaderTestAccess::render(*reader, 0);
  renderer.textTrace = nullptr;
  renderer.textTraceContext = nullptr;
  EXPECT_NE(drawnCodepoints.end(), std::find(drawnCodepoints.begin(), drawnCodepoints.end(), '-'));

  const auto path = TxtReaderTestAccess::cache(*reader);
  const auto& index = readerTestFiles[path + "/index.bin"];
  ASSERT_GE(index.size(), txt::INDEX_HEADER_CURRENT_SIZE);
  EXPECT_EQ(txt::INDEX_VERSION_V9, index[4]);
  EXPECT_EQ(1, index[49]);
}

TEST_F(TxtReaderIntegration, ProductionReaderExpandsOnlyAsciiSpacesAcrossTheGoldenCorpus) {
  const std::vector<std::string> corpus = {
      "가나다라마바사 아자차카타파하 가나다라마바사 아자차카타파하",
      "가나다라마바사아자차카타파하가나다라마바사아자차카타파하",
      "This is a very long internationalization demonstration sentence.",
      "한글 문장 internationalization 한글 문장 테스트입니다.",
  };
  const std::vector<size_t> expectedSpaceCounts = {3, 0, 7, 5};

  for (size_t sampleIndex = 0; sampleIndex < corpus.size(); ++sampleIndex) {
    SCOPED_TRACE(corpus[sampleIndex]);
    open(corpus[sampleIndex]);
    TxtReaderTestAccess::geometry(*reader, 1000, 2);
    txt::PageLine softLine{corpus[sampleIndex], 0, corpus[sampleIndex].size(), false, false};
    txt::PageLine finalLine{"X", 0, 1, false, true};
    const std::vector<txt::PageLine> lines{softLine, finalLine};

    const auto measured = TxtReaderTestAccess::measure(*reader, softLine.text.c_str());
    ASSERT_TRUE(measured.ok());
    EXPECT_EQ(expectedSpaceCounts[sampleIndex], measured.eligibleSpaceCount);
    EXPECT_EQ(expectedSpaceCounts[sampleIndex] == 0 ? 0 : 8, measured.minimumEligibleSpaceAdvance);

    const auto natural = drawLines(lines, CrossPointSettings::LEFT_ALIGN);
    const auto justified = drawLines(lines, CrossPointSettings::JUSTIFIED);
    ASSERT_EQ(natural.glyphs.size(), justified.glyphs.size());
    ASSERT_GE(natural.lineAdvances.size(), 1U);
    ASSERT_GE(justified.lineAdvances.size(), 1U);
    ASSERT_GE(natural.lineGlyphCounts.size(), 1U);
    ASSERT_GE(justified.lineGlyphCounts.size(), 1U);
    EXPECT_EQ(measured.advance, natural.lineAdvances[0]);
    EXPECT_EQ(measured.advance + static_cast<int>(measured.eligibleSpaceCount *
                                                  measured.minimumEligibleSpaceAdvance),
              justified.lineAdvances[0]);

    EXPECT_EQ(natural.lineGlyphCounts[0], justified.lineGlyphCounts[0]);
    size_t spacesSeen = 0;
    for (size_t glyphIndex = 0; glyphIndex < natural.lineGlyphCounts[0]; ++glyphIndex) {
      EXPECT_EQ(natural.glyphs[glyphIndex].codepoint, justified.glyphs[glyphIndex].codepoint);
      EXPECT_EQ(static_cast<int>(spacesSeen * measured.minimumEligibleSpaceAdvance),
                justified.glyphs[glyphIndex].x - natural.glyphs[glyphIndex].x);
      if (natural.glyphs[glyphIndex].codepoint == ' ') ++spacesSeen;
    }
    EXPECT_EQ(measured.eligibleSpaceCount, spacesSeen);
  }
}

TEST_F(TxtReaderIntegration, JustificationUsesBoundedSpaceAdvanceAndHonorsLineExclusions) {
  SETTINGS.paragraphAlignment = CrossPointSettings::JUSTIFIED;
  open("A V");
  TxtReaderTestAccess::geometry(*reader, 29, 2);
  txt::PageLine softLine{"A V", 0, 3, false, false};
  txt::PageLine finalLine{"X", 3, 4, false, true};
  auto trace = drawLines({softLine, finalLine}, CrossPointSettings::JUSTIFIED);
  ASSERT_GE(trace.lineAdvances.size(), 2U);
  EXPECT_EQ(29, trace.lineAdvances[0]);
  ASSERT_GE(trace.glyphs.size(), 4U);
  EXPECT_EQ('A', trace.glyphs[0].codepoint);
  EXPECT_EQ(' ', trace.glyphs[1].codepoint);
  EXPECT_EQ('V', trace.glyphs[2].codepoint);
  EXPECT_EQ(0, trace.glyphs[0].x);
  EXPECT_EQ(8, trace.glyphs[1].x);
  EXPECT_EQ(21, trace.glyphs[2].x);

  TxtReaderTestAccess::geometry(*reader, 41, 2);
  trace = drawLines({softLine, finalLine}, CrossPointSettings::JUSTIFIED);
  ASSERT_GE(trace.lineAdvances.size(), 2U);
  EXPECT_EQ(32, trace.lineAdvances[0]);
  EXPECT_EQ(24, trace.glyphs[2].x);

  TxtReaderTestAccess::geometry(*reader, 45, 2);
  txt::PageLine twoSpaces{"A  V", 0, 4, false, false};
  trace = drawLines({twoSpaces, finalLine}, CrossPointSettings::JUSTIFIED);
  ASSERT_GE(trace.lineAdvances.size(), 2U);
  EXPECT_EQ(44, trace.lineAdvances[0]);
  EXPECT_EQ(8, trace.glyphs[1].x);
  EXPECT_EQ(22, trace.glyphs[2].x);
  EXPECT_EQ(36, trace.glyphs[3].x);

  TxtReaderTestAccess::geometry(*reader, 80, 2);
  trace = drawLines({softLine}, CrossPointSettings::JUSTIFIED);
  ASSERT_EQ(1U, trace.lineAdvances.size());
  EXPECT_EQ(24, trace.lineAdvances[0]); // The page-final line keeps natural spacing.
  EXPECT_EQ(16, trace.glyphs[2].x);
  softLine.paragraphEnd = true;
  trace = drawLines({softLine, finalLine}, CrossPointSettings::JUSTIFIED);
  EXPECT_EQ(24, trace.lineAdvances[0]); // Paragraph-final lines keep natural spacing.
  EXPECT_EQ(16, trace.glyphs[2].x);
  trace = drawLines({softLine, finalLine}, CrossPointSettings::LEFT_ALIGN);
  EXPECT_EQ(24, trace.lineAdvances[0]);
  EXPECT_EQ(16, trace.glyphs[2].x);

  txt::PageLine rtlLine{"\xD9\x84\xD8\xA7 V", 0, 5, false, false};
  const auto rtlNatural = TxtReaderTestAccess::measure(*reader, rtlLine.text.c_str());
  ASSERT_TRUE(rtlNatural.ok());
  trace = drawLines({rtlLine, finalLine}, CrossPointSettings::JUSTIFIED);
  ASSERT_GE(trace.lineAdvances.size(), 2U);
  EXPECT_EQ(rtlNatural.advance, trace.lineAdvances[0]); // RTL paragraphs are excluded.
}

TEST_F(TxtReaderIntegration, TxtSpaceExpansionLeavesPaginationAndSourcePartitionsIndependentOfAlignment) {
  const std::vector<std::string> corpus = {
      "가나다라마바사 아자차카타파하 가나다라마바사 아자차카타파하",
      "가나다라마바사아자차카타파하가나다라마바사아자차카타파하",
      "This is a very long internationalization demonstration sentence.",
      "한글 문장 internationalization 한글 문장 테스트입니다.",
  };
  using PageRecord = std::tuple<std::string, size_t, size_t, bool, bool>;
  struct LayoutSnapshot {
    std::vector<size_t> offsets;
    std::vector<std::vector<PageRecord>> pages;
    std::vector<size_t> nextOffsets;
  };

  for (const auto& sample : corpus) {
    SCOPED_TRACE(sample);
    for (const bool hyphenationEnabled : {false, true}) {
      LayoutSnapshot left;
      for (const uint8_t alignment : {CrossPointSettings::LEFT_ALIGN, CrossPointSettings::JUSTIFIED}) {
        SETTINGS.paragraphAlignment = alignment;
        SETTINGS.hyphenationEnabled = hyphenationEnabled;
        open(sample);
        TxtReaderTestAccess::geometry(*reader, 72, 4);
        ASSERT_TRUE(TxtReaderTestAccess::build(*reader));

        LayoutSnapshot current;
        current.offsets = TxtReaderTestAccess::offsets(*reader);
        checkPartitions(current.offsets, sample.size());
        for (const size_t offset : current.offsets) {
          std::vector<txt::PageLine> lines;
          size_t nextOffset = 0;
          ASSERT_TRUE(TxtReaderTestAccess::page(*reader, offset, lines, nextOffset));
          std::vector<PageRecord> page;
          for (const auto& line : lines) {
            page.emplace_back(line.text, line.sourceStart, line.sourceEnd, line.paragraphStart, line.paragraphEnd);
          }
          current.pages.push_back(std::move(page));
          current.nextOffsets.push_back(nextOffset);

          ASSERT_TRUE(TxtReaderTestAccess::renderLines(*reader, lines));
          const auto afterRender = TxtReaderTestAccess::currentLines(*reader);
          ASSERT_EQ(lines.size(), afterRender.size());
          for (size_t i = 0; i < lines.size(); ++i) EXPECT_TRUE(sameLine(lines[i], afterRender[i]));
          EXPECT_EQ(current.offsets, TxtReaderTestAccess::offsets(*reader));
        }

        if (alignment == CrossPointSettings::LEFT_ALIGN) {
          left = std::move(current);
        } else {
          EXPECT_EQ(left.offsets, current.offsets);
          EXPECT_EQ(left.pages, current.pages);
          EXPECT_EQ(left.nextOffsets, current.nextOffsets);
        }
      }
    }
  }
}

TEST_F(TxtReaderIntegration, InternationalizationHyphenBreakAndSourceOffsetSurviveSpaceJustification) {
  installAsciiHyphenationFont(true, true);
  SETTINGS.paragraphAlignment = CrossPointSettings::JUSTIFIED;
  SETTINGS.hyphenationEnabled = true;
  open("internationalization");
  TxtReaderTestAccess::geometry(*reader, 108, 2);
  std::vector<txt::PageLine> lines;
  size_t nextOffset = 0;
  ASSERT_TRUE(TxtReaderTestAccess::page(*reader, 0, lines, nextOffset));
  ASSERT_GE(lines.size(), 2U);
  EXPECT_EQ("international-", lines[0].text);
  EXPECT_EQ(0U, lines[0].sourceStart);
  EXPECT_EQ(13U, lines[0].sourceEnd);
  EXPECT_EQ("ization", lines[1].text);
  EXPECT_EQ(13U, lines[1].sourceStart);
  EXPECT_EQ(20U, lines[1].sourceEnd);
  EXPECT_EQ(20U, nextOffset);
  ASSERT_TRUE(TxtReaderTestAccess::build(*reader));
  const auto offsets = TxtReaderTestAccess::offsets(*reader);
  checkPartitions(offsets, std::string("internationalization").size());
  EXPECT_EQ(1U, offsets.size());
  EXPECT_EQ(0U, offsets.front());

  SETTINGS.hyphenationEnabled = false;
  open("internationalization");
  TxtReaderTestAccess::geometry(*reader, 108, 2);
  lines.clear();
  nextOffset = 0;
  ASSERT_TRUE(TxtReaderTestAccess::page(*reader, 0, lines, nextOffset));
  ASSERT_GE(lines.size(), 2U);
  EXPECT_FALSE(lines[0].text.ends_with('-'));
  EXPECT_EQ(20U, nextOffset);
}

TEST_F(TxtReaderIntegration, ReplacementSyntheticHyphenSkipsEnglishCandidates) {
  installAsciiHyphenationFont(true, false);
  SETTINGS.hyphenationEnabled = true;
  open("extraordinary");
  TxtReaderTestAccess::geometry(*reader, 48, 1);
  std::vector<txt::PageLine> lines;
  size_t next = 0;
  ASSERT_TRUE(TxtReaderTestAccess::page(*reader, 0, lines, next));
  ASSERT_EQ(1u, lines.size());
  EXPECT_FALSE(lines[0].text.ends_with('-'));
  EXPECT_EQ(lines[0].text.size(), next);
}

TEST_F(TxtReaderIntegration, MissingSyntheticHyphenSkipsEnglishCandidates) {
  installAsciiHyphenationFont(false, false);
  SETTINGS.hyphenationEnabled = true;
  open("extraordinary");
  TxtReaderTestAccess::geometry(*reader, 48, 1);
  std::vector<txt::PageLine> lines;
  size_t next = 0;
  ASSERT_TRUE(TxtReaderTestAccess::page(*reader, 0, lines, next));
  ASSERT_EQ(1u, lines.size());
  EXPECT_FALSE(lines[0].text.ends_with('-'));
  EXPECT_EQ(lines[0].text.size(), next);
}

TEST_F(TxtReaderIntegration, DashMetricIoFailureAbortsThePageWithoutSourceConsumption) {
  installAsciiHyphenationFont(true, true);
  SETTINGS.hyphenationEnabled = true;
  open("extraordinary");
  TxtReaderTestAccess::geometry(*reader, 48, 1);
  ASSERT_TRUE(TxtReaderTestAccess::primeTextMetrics(*reader, "extraordinary"));
  sdFontTestReadFailure = true;

  std::vector<txt::PageLine> lines;
  size_t next = 99;
  EXPECT_FALSE(TxtReaderTestAccess::page(*reader, 0, lines, next));
  EXPECT_TRUE(lines.empty());
  EXPECT_EQ(0u, next);
  sdFontTestReadFailure = false;
}

TEST_F(TxtReaderIntegration, HyphenationSettingInvalidatesV9AndRestoresTheSourceAnchor) {
  SETTINGS.hyphenationEnabled = true;
  std::string text;
  for (int i = 0; i < 1200; ++i) text += "AVAVAVAV\n";
  open(text);
  ASSERT_TRUE(TxtReaderTestAccess::initialize(*reader));
  const auto oldOffsets = TxtReaderTestAccess::offsets(*reader);
  ASSERT_GT(oldOffsets.size(), 2u);
  const auto path = TxtReaderTestAccess::cache(*reader);
  auto& oldIndex = readerTestFiles[path + "/index.bin"];
  ASSERT_GE(oldIndex.size(), txt::INDEX_HEADER_CURRENT_SIZE);
  ASSERT_EQ(txt::INDEX_VERSION_V9, oldIndex[4]);
  ASSERT_EQ(1, oldIndex[49]);

  const auto snapshot = TxtReaderTestAccess::settingsSnapshot(*reader);
  const size_t sourceAnchor = oldOffsets[1];
  SETTINGS.hyphenationEnabled = false;
  TxtReaderTestAccess::settingsReturned(*reader, snapshot, sourceAnchor, true);
  ASSERT_TRUE(TxtReaderTestAccess::initialize(*reader));
  const auto newOffsets = TxtReaderTestAccess::offsets(*reader);
  EXPECT_EQ(static_cast<int>(txt::PageLayout::pageForSourceAnchor(newOffsets, sourceAnchor)),
            TxtReaderTestAccess::currentPage(*reader));
  const auto& newIndex = readerTestFiles[path + "/index.bin"];
  ASSERT_GE(newIndex.size(), txt::INDEX_HEADER_CURRENT_SIZE);
  EXPECT_EQ(txt::INDEX_VERSION_V9, newIndex[4]);
  EXPECT_EQ(0, newIndex[49]);
}

TEST_F(TxtReaderIntegration, ColdWarmEvictedReopenSourcePartitionsRemainIdentical) {
  SETTINGS.paragraphAlignment=CrossPointSettings::JUSTIFIED; SETTINGS.paragraphIndentSpaces=1; SETTINGS.extraParagraphSpacing=1;
  const std::string text="AV fi X\xEA\xB0\x80\xE4\xB8\xAD\r\n\r\nA V fi\n" + std::string(8200,'X') + "\xEA\xB0\x80\n" + std::string("A\xE2\x28\xA1",4) + "\n";
  open(text); ASSERT_TRUE(TxtReaderTestAccess::build(*reader));
  const auto offsets=TxtReaderTestAccess::offsets(*reader); ASSERT_GT(offsets.size(),100u); checkPartitions(offsets,text.size());
  ASSERT_TRUE(TxtReaderTestAccess::save(*reader));
  { auto scope=manager->createPrewarmScope(); renderer.drawText(42,0,0,"AV fi X"); scope.endScanAndPrewarm(); }
  checkPartitions(offsets,text.size()); font.releaseResidentCaches(); checkPartitions(offsets,text.size());
  reader=std::make_unique<TxtReaderActivity>(renderer,input,"/book.txt",false); ASSERT_TRUE(TxtReaderTestAccess::open(*reader));
  ASSERT_TRUE(TxtReaderTestAccess::load(*reader)); EXPECT_EQ(offsets,TxtReaderTestAccess::offsets(*reader)); checkPartitions(offsets,text.size());
}
TEST_F(TxtReaderIntegration, FailedBuildAfterFirstPagePreservesPublishedIndexAndProgress) {
  open("AVAVAVAVAVAV\n"); ASSERT_TRUE(TxtReaderTestAccess::build(*reader)); ASSERT_TRUE(TxtReaderTestAccess::save(*reader));
  const auto offsets=TxtReaderTestAccess::offsets(*reader); const auto path=TxtReaderTestAccess::cache(*reader);
  const auto index=readerTestFiles[path+"/index.bin"]; readerTestFiles[path+"/progress.bin"]={1,0,0,0,2,0,0,0};
  const auto progress=readerTestFiles[path+"/progress.bin"];
  readerTestPathReads.clear(); readerTestShortReadPath="/book.txt"; readerTestShortReadAfter=1;
  EXPECT_FALSE(TxtReaderTestAccess::build(*reader)); EXPECT_EQ(offsets,TxtReaderTestAccess::offsets(*reader));
  EXPECT_EQ(index,readerTestFiles[path+"/index.bin"]); EXPECT_EQ(progress,readerTestFiles[path+"/progress.bin"]);
  readerTestShortReadPath.clear(); reader=std::make_unique<TxtReaderActivity>(renderer,input,"/book.txt",false);
  ASSERT_TRUE(TxtReaderTestAccess::open(*reader)); ASSERT_TRUE(TxtReaderTestAccess::load(*reader)); EXPECT_EQ(offsets,TxtReaderTestAccess::offsets(*reader));
}
TEST_F(TxtReaderIntegration, MetadataAndPageBufferFailureDoNotConsumeOrSaveProgress) {
  open("AVAV\n"); ASSERT_TRUE(TxtReaderTestAccess::build(*reader)); const auto path=TxtReaderTestAccess::cache(*reader);
  readerTestFiles[path+"/progress.bin"]={0,0,0,0,0,0,0,0}; const auto progress=readerTestFiles[path+"/progress.bin"];
  TxtReaderTestAccess::clearMetrics(*reader); readerTestShortReadPath="typography.cpfont";
  std::vector<txt::PageLine> lines; size_t next=0;
  EXPECT_FALSE(TxtReaderTestAccess::page(*reader,0,lines,next)); EXPECT_TRUE(lines.empty()); EXPECT_EQ(0u,next);
  TxtReaderTestAccess::render(*reader); EXPECT_EQ(0u,readerTestProgressWrites); EXPECT_EQ(progress,readerTestFiles[path+"/progress.bin"]);
  readerTestShortReadPath.clear(); readerTestFailNextNothrowBytes=readerTestFiles["/book.txt"].size()+1;
  EXPECT_FALSE(TxtReaderTestAccess::build(*reader)); EXPECT_EQ(0u,readerTestFailNextNothrowBytes);
}
TEST_F(TxtReaderIntegration, IndexAllocationAndShortCacheWritesPreserveLastValidCache) {
  open("AVAVAVAV\n"); ASSERT_TRUE(TxtReaderTestAccess::build(*reader)); ASSERT_TRUE(TxtReaderTestAccess::save(*reader));
  const auto path=TxtReaderTestAccess::cache(*reader); const auto index=readerTestFiles[path+"/index.bin"];
  const auto offsets=TxtReaderTestAccess::offsets(*reader); readerTestFailNextNothrowBytes=16*sizeof(size_t);
  EXPECT_FALSE(TxtReaderTestAccess::build(*reader)); EXPECT_EQ(offsets,TxtReaderTestAccess::offsets(*reader));
  readerTestShortWritePath=path+"/index.bin.tmp"; EXPECT_FALSE(TxtReaderTestAccess::save(*reader)); EXPECT_EQ(index,readerTestFiles[path+"/index.bin"]);
  readerTestShortWritePath.clear(); readerTestRenameFailure=true; EXPECT_FALSE(TxtReaderTestAccess::save(*reader)); EXPECT_EQ(index,readerTestFiles[path+"/index.bin"]);
}
TEST_F(TxtReaderIntegration, FailedBitmapDrawDoesNotSaveProgressButSuccessfulRetryDoes) {
  open("AV fi\n"); ASSERT_TRUE(TxtReaderTestAccess::build(*reader)); const auto path=TxtReaderTestAccess::cache(*reader);
  readerTestFiles[path+"/progress.bin"]={0,0,0,0,0,0,0,0}; const auto progress=readerTestFiles[path+"/progress.bin"];
  font.releaseResidentCaches(); sdFontTestFailArrayAllocations=true;
  TxtReaderTestAccess::render(*reader); EXPECT_EQ(0u,readerTestProgressWrites); EXPECT_EQ(progress,readerTestFiles[path+"/progress.bin"]);
  sdFontTestFailArrayAllocations=false; TxtReaderTestAccess::render(*reader); EXPECT_GT(readerTestProgressWrites,0u);
}
TEST_F(TxtReaderIntegration, InitializationMeasuresIndentationCanonicallyAndPropagatesContextFailure) {
  readerTestFiles["/book.txt"]={'A','V','\n'}; SETTINGS.paragraphIndentSpaces=2;
  reader=std::make_unique<TxtReaderActivity>(renderer,input,"/book.txt",false);
  ASSERT_TRUE(TxtReaderTestAccess::loadBook(*reader));
  readerTestFailNextNothrowBytes=sizeof(SdCardFont::TxtMetricContext);
  EXPECT_FALSE(TxtReaderTestAccess::initialize(*reader)); EXPECT_EQ(0u,readerTestFailNextNothrowBytes);
  ASSERT_TRUE(TxtReaderTestAccess::initialize(*reader)); EXPECT_EQ(16,TxtReaderTestAccess::indent(*reader));
}


TEST_F(TxtReaderIntegration, ChunkCrLfMalformedAndOversizedSourceRangesSurviveReopen) {
  const std::vector<std::string> inputs={"", "\r\n\n", "AV\r\nfi", std::string(8191,'X')+"\xEA\xB0\x80\n", std::string(8191,'X')+"\xEA\xB0\x80\r\n", std::string(8191,'X')+"\xEA\xB0\x80", std::string("A\xF0\x9F\x98\x80\xC2\xA2\xE2\x82\xAC\n")};
  for(const auto& text:inputs) {
    open(text); TxtReaderTestAccess::geometry(*reader,80000,1);
    ASSERT_TRUE(TxtReaderTestAccess::build(*reader)); const auto offsets=TxtReaderTestAccess::offsets(*reader);
    if(!text.empty()) checkPartitions(offsets,text.size()); ASSERT_TRUE(TxtReaderTestAccess::save(*reader));
    reader=std::make_unique<TxtReaderActivity>(renderer,input,"/book.txt",false); ASSERT_TRUE(TxtReaderTestAccess::open(*reader));
    TxtReaderTestAccess::geometry(*reader,80000,1); ASSERT_TRUE(TxtReaderTestAccess::load(*reader));
    EXPECT_EQ(offsets,TxtReaderTestAccess::offsets(*reader)); if(!text.empty()) checkPartitions(offsets,text.size());
  }
  open("AV\n"); TxtReaderTestAccess::geometry(*reader,1,1); ASSERT_TRUE(TxtReaderTestAccess::build(*reader)); checkPartitions(TxtReaderTestAccess::offsets(*reader),3);
}
TEST_F(TxtReaderIntegration, FailedPublishRollbackRetainsBackupAndReopenRecoversIt) {
  open("AVAV\n"); ASSERT_TRUE(TxtReaderTestAccess::build(*reader)); ASSERT_TRUE(TxtReaderTestAccess::save(*reader));
  const auto path=TxtReaderTestAccess::cache(*reader); const auto index=readerTestFiles[path+"/index.bin"];
  readerTestRenameFailureDestination=path+"/index.bin"; EXPECT_FALSE(TxtReaderTestAccess::save(*reader));
  EXPECT_EQ(index,readerTestFiles[path+"/index.bin.bak"]); EXPECT_FALSE(Storage.exists((path+"/index.bin").c_str()));
  readerTestRenameFailureDestination.clear(); ASSERT_TRUE(TxtReaderTestAccess::load(*reader)); EXPECT_EQ(index,readerTestFiles[path+"/index.bin"]);
  readerTestFiles[path+"/progress.bin"]={1,0,0,0,2,0,0,0}; const auto progress=readerTestFiles[path+"/progress.bin"];
  readerTestRenameFailureDestination=path+"/progress.bin"; TxtReaderTestAccess::saveProgress(*reader);
  EXPECT_EQ(progress,readerTestFiles[path+"/progress.bin.bak"]);
  readerTestRenameFailureDestination.clear(); txt::ProgressRecord record; ASSERT_TRUE(TxtReaderTestAccess::progress(*reader,record)); EXPECT_EQ(2u,record.sourceAnchor);
}
TEST_F(TxtReaderIntegration, CheckedHandleFailureDoesNotTruncateIndexOrProgress) {
  open("AVAV\n"); ASSERT_TRUE(TxtReaderTestAccess::build(*reader)); ASSERT_TRUE(TxtReaderTestAccess::save(*reader));
  const auto path=TxtReaderTestAccess::cache(*reader); const auto index=readerTestFiles[path+"/index.bin"];
  readerTestFiles[path+"/progress.bin"]={1,0,0,0,2,0,0,0}; const auto progress=readerTestFiles[path+"/progress.bin"];
  sdFontTestHandleFailure=true; EXPECT_FALSE(TxtReaderTestAccess::save(*reader)); TxtReaderTestAccess::saveProgress(*reader);
  EXPECT_EQ(index,readerTestFiles[path+"/index.bin"]); EXPECT_EQ(progress,readerTestFiles[path+"/progress.bin"]); EXPECT_EQ(0u,readerTestProgressWrites);
}

TEST_F(TxtReaderIntegration, ActualRtlDrawUsesCanonicalPolicyAndPreservesSavedSourceAnchor) {
  SETTINGS.paragraphAlignment=CrossPointSettings::JUSTIFIED; SETTINGS.paragraphIndentSpaces=1;
  open("\xD9\x84\xD8\xA7 AV\nAVAVAV\n"); ASSERT_TRUE(TxtReaderTestAccess::build(*reader)); ASSERT_TRUE(TxtReaderTestAccess::save(*reader));
  const auto offsets=TxtReaderTestAccess::offsets(*reader); checkPartitions(offsets,readerTestFiles["/book.txt"].size());
  TxtReaderTestAccess::render(*reader,1); txt::ProgressRecord saved;
  ASSERT_TRUE(TxtReaderTestAccess::progress(*reader,saved)); EXPECT_TRUE(saved.hasSourceAnchor); EXPECT_EQ(offsets[1],saved.sourceAnchor);
  font.releaseResidentCaches(); reader=std::make_unique<TxtReaderActivity>(renderer,input,"/book.txt",false);
  ASSERT_TRUE(TxtReaderTestAccess::open(*reader)); ASSERT_TRUE(TxtReaderTestAccess::load(*reader)); ASSERT_TRUE(TxtReaderTestAccess::progress(*reader,saved));
  EXPECT_EQ(1u,txt::PageLayout::pageForSourceAnchor(TxtReaderTestAccess::offsets(*reader),saved.sourceAnchor));
}

TEST_F(TxtReaderIntegration, EmptyInputDoesNotAttemptPageReconstructionOrOverwriteProgress) {
  open(""); ASSERT_TRUE(TxtReaderTestAccess::build(*reader)); ASSERT_TRUE(TxtReaderTestAccess::save(*reader));
  EXPECT_EQ(std::vector<size_t>({0}),TxtReaderTestAccess::offsets(*reader));
  TxtReaderTestAccess::render(*reader); EXPECT_EQ(0u,readerTestProgressWrites);
}

TEST_F(TxtReaderIntegration, ReaderOptionsRebuildOnlyLayoutAndRestoreBySourceAnchor) {
  std::string text;
  text.reserve(30000);
  for (int i = 0; i < 150; ++i) text += std::string(180, 'A') + "\n";
  open(text);
  ASSERT_TRUE(TxtReaderTestAccess::initialize(*reader));
  const auto oldOffsets = TxtReaderTestAccess::offsets(*reader);
  ASSERT_GT(oldOffsets.size(), 3u);
  const size_t anchor = oldOffsets[2];
  TxtReaderTestAccess::setCurrentPage(*reader, 2);
  const auto before = TxtReaderTestAccess::settingsSnapshot(*reader);

  EXPECT_FALSE(TxtReaderTestAccess::settingsChanged(*reader, before));
  SETTINGS.textAntiAliasing = true;
  EXPECT_FALSE(TxtReaderTestAccess::settingsChanged(*reader, before));
  TxtReaderTestAccess::settingsReturned(*reader, before, anchor, true);
  EXPECT_TRUE(TxtReaderTestAccess::initialized(*reader));
  EXPECT_FALSE(TxtReaderTestAccess::hasPendingAnchor(*reader));
  EXPECT_EQ(oldOffsets, TxtReaderTestAccess::offsets(*reader));

  SETTINGS.screenMargin = 48;
  EXPECT_TRUE(TxtReaderTestAccess::settingsChanged(*reader, before));
  TxtReaderTestAccess::settingsReturned(*reader, before, anchor, true);
  EXPECT_FALSE(TxtReaderTestAccess::initialized(*reader));
  EXPECT_TRUE(TxtReaderTestAccess::hasPendingAnchor(*reader));
  EXPECT_TRUE(TxtReaderTestAccess::offsets(*reader).empty());

  ASSERT_TRUE(TxtReaderTestAccess::initialize(*reader));
  const auto newOffsets = TxtReaderTestAccess::offsets(*reader);
  ASSERT_FALSE(newOffsets.empty());
  const size_t restoredPage = txt::PageLayout::pageForSourceAnchor(newOffsets, anchor);
  EXPECT_EQ(static_cast<int>(restoredPage), TxtReaderTestAccess::currentPage(*reader));
  EXPECT_LE(newOffsets[restoredPage], anchor);
  EXPECT_FALSE(TxtReaderTestAccess::hasPendingAnchor(*reader));
}

TEST_F(TxtReaderIntegration, ReaderOptionsRefreshMetricSessionWithoutRepagination) {
  std::string text;
  for (int i = 0; i < 150; ++i) text += "AVAVAVAV\n";
  open(text);
  ASSERT_TRUE(TxtReaderTestAccess::initialize(*reader));
  const auto oldOffsets = TxtReaderTestAccess::offsets(*reader);
  ASSERT_GT(oldOffsets.size(), 2u);
  constexpr int page = 1;
  TxtReaderTestAccess::setCurrentPage(*reader, page);
  const auto before = TxtReaderTestAccess::settingsSnapshot(*reader);

  ASSERT_TRUE(reloadedFont.load("typography.cpfont"));
  renderer.unregisterSdCardFont(42);
  renderer.insertFont(42, EpdFontFamily(reloadedFont.getEpdFont()));
  renderer.registerSdCardFont(42, &reloadedFont);
  sdFonts[42] = &reloadedFont;

  TxtReaderTestAccess::settingsReturned(*reader, before, oldOffsets[page], true);
  EXPECT_TRUE(TxtReaderTestAccess::initialized(*reader));
  EXPECT_EQ(page, TxtReaderTestAccess::currentPage(*reader));
  EXPECT_EQ(oldOffsets, TxtReaderTestAccess::offsets(*reader));
  EXPECT_TRUE(TxtReaderTestAccess::renderAndCheck(*reader, page));
  EXPECT_EQ(page, TxtReaderTestAccess::currentPage(*reader));
  EXPECT_EQ(oldOffsets, TxtReaderTestAccess::offsets(*reader));
}
