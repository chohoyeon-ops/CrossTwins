#include <GfxRenderer.h>
#include <FontCacheManager.h>
#include <FontPsram.h>
#include <gtest/gtest.h>
#include <algorithm>
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

struct TxtReaderTestAccess {
  static bool open(TxtReaderActivity& r) {
    if (!r.loadBook()) return false;
    r.cachedFontId=42; r.cachedFontPointSize=12; r.viewportWidth=15; r.cachedViewportHeight=20;
    r.lineAdvance=10; r.linesPerPage=2; r.cachedParagraphAlignment=SETTINGS.paragraphAlignment;
    r.paragraphGap=SETTINGS.extraParagraphSpacing ? 5 : 0; r.paragraphIndentWidth=SETTINGS.paragraphIndentSpaces*5;
    r.metricContext=std::make_unique<SdCardFont::TxtMetricContext>(); r.bodyTypography.metrics=r.metricContext.get();
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
  static void render(TxtReaderActivity& r,int page=0) { r.initialized=true; r.currentPage=page; r.renderBook(); }
  static void clearMetrics(TxtReaderActivity& r) { r.metricContext->clearCache(); }
  static void geometry(TxtReaderActivity& r,int width,int lines) { r.viewportWidth=width; r.linesPerPage=lines; r.cachedViewportHeight=lines*r.lineAdvance; }
  static bool progress(TxtReaderActivity& r,txt::ProgressRecord& p) { return r.readSavedProgress(p); }
  static void saveProgress(TxtReaderActivity& r) { r.saveProgress(); }
  static int indent(TxtReaderActivity& r) { return r.paragraphIndentWidth; }
  static bool legacyAnchor(TxtReaderActivity& r, uint16_t page, uint32_t& anchor) {
    return r.deriveLegacySourceAnchor(page, anchor);
  }
  static int currentPage(const TxtReaderActivity& r) { return r.currentPage; }
};

class TxtReaderIntegration : public ::testing::Test {
 protected:
  SdCardFont font;
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
};

TEST_F(TxtReaderIntegration, V8CacheIdentityRejectsLegacyVersionsAndPreservesProgressConsumers) {
  std::string text;
  text.reserve(1200);
  for (int i = 0; i < 120; ++i) text += "AVAVAVAV\n";
  for (const uint8_t legacyVersion : {txt::INDEX_VERSION_V6, txt::INDEX_VERSION_V7}) {
    SCOPED_TRACE(legacyVersion);
    open(text);
    ASSERT_TRUE(TxtReaderTestAccess::build(*reader));
    const auto oldOffsets = TxtReaderTestAccess::offsets(*reader);
    ASSERT_GT(oldOffsets.size(), 2u);
    ASSERT_TRUE(TxtReaderTestAccess::save(*reader));

    const auto cachePath = TxtReaderTestAccess::cache(*reader);
    auto& indexBytes = readerTestFiles[cachePath + "/index.bin"];
    ASSERT_EQ(txt::INDEX_HEADER_V8_SIZE + oldOffsets.size() * sizeof(uint32_t), indexBytes.size());
    EXPECT_EQ(txt::INDEX_VERSION_V8, indexBytes[4]);
    EXPECT_EQ(txt::INDEX_HEADER_V8_SIZE, txt::INDEX_HEADER_CURRENT_SIZE);

    txt::IndexHeader currentHeader;
    ASSERT_TRUE(txt::parseIndexHeader(indexBytes.data(), txt::INDEX_HEADER_CURRENT_SIZE, indexBytes.size(),
                                      currentHeader));
    EXPECT_EQ(txt::INDEX_VERSION_V8, currentHeader.version);
    EXPECT_EQ(oldOffsets.size(), currentHeader.pageCount);
    readerTestFiles[cachePath + "/progress.bin"] = {1, 0, 0, 0};
    EXPECT_EQ(static_cast<int>(std::min<size_t>(100, 200ULL / oldOffsets.size())),
              loadBookProgress("/book.txt"));

    const uint16_t legacyPage = 1;
    indexBytes[4] = legacyVersion;
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
    EXPECT_EQ(txt::INDEX_VERSION_V8, rebuiltIndex[4]);
  }
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
