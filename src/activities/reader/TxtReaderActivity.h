#pragma once

#include <GfxRenderer.h>
#include <Txt.h>
#include <TxtIndexFormat.h>
#include <TxtPageLayout.h>
#include <TxtPageOffsets.h>
#include <Epub/hyphenation/Hyphenator.h>

#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#ifdef CROSSPOINT_TXT_READER_TEST
#include <TxtReaderHostBase.h>
#else
#include "ReaderActivity.h"
#endif

class TxtReaderActivity final : public ReaderActivity {
  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;

  // Streaming text reader - stores file offsets for each page
  txt::PageOffsets pageOffsets;
  std::vector<txt::PageLine> currentPageLines;
  int linesPerPage = 0;
  int lineAdvance = 1;
  int viewportWidth = 0;
  int cachedViewportHeight = 0;
  int paragraphGap = 0;
  int paragraphIndentWidth = 0;
  bool initialized = false;
  bool cacheRejected = false;
  bool hasPendingSourceAnchor = false;
  size_t pendingSourceAnchor = 0;
  std::unique_ptr<SdCardFont::TxtMetricContext> metricContext;
  std::unique_ptr<LiangWorkspace> hyphenationWorkspace;
  GfxRenderer::TextTypography bodyTypography{GfxRenderer::TypographyPolicy::TxtDeterministic, nullptr};
  GfxRenderer::TextResult lastTextResult;

  // Cached settings for cache validation
  int cachedFontId = 0;
  uint8_t cachedFontPointSize = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedExtraParagraphSpacing = 0;
  uint8_t cachedParagraphIndentSpaces = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  bool cachedHyphenationEnabled = false;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  struct LayoutSettingsSnapshot {
    int fontId = 0;
    uint8_t fontPointSize = 0;
    uint8_t lineSpacing = 0;
    uint8_t screenMargin = 0;
    uint8_t extraParagraphSpacing = 0;
    uint8_t paragraphIndentSpaces = 0;
    uint8_t paragraphAlignment = 0;
    bool hyphenationEnabled = false;
  };

  bool renderPage(GfxRenderer& renderer);
  bool initializeReader(GfxRenderer& renderer);
  bool loadPageAtOffset(const GfxRenderer& renderer, size_t offset, std::vector<txt::PageLine>& outLines,
                        size_t& nextOffset);
  bool buildPageIndex(GfxRenderer& renderer);
  bool loadPageIndexCache();
  bool savePageIndexCache() const;
  void saveProgress() const;
  bool readSavedProgress(txt::ProgressRecord& progress) const;
  bool deriveLegacySourceAnchor(uint16_t page, uint32_t& sourceAnchor) const;
  void applySavedProgress(const txt::ProgressRecord& progress, bool hasProgress);
  void renderStatusBar() const;

  bool loadBook() override;
  std::string getBookTitle() const override { return txt ? txt->getTitle() : ""; }
  void renderBook() override;
#ifndef CROSSPOINT_TXT_READER_TEST
  bool handleFormatInput() override;
  void openTextSettings();
#endif
  LayoutSettingsSnapshot captureLayoutSettings() const;
  bool layoutSettingsChanged(const LayoutSettingsSnapshot& previous) const;
  bool refreshTxtMetricSession();
  static txt::PageLayout::HyphenationStatus findTxtHyphenationBreaks(void* context, const char* word,
                                                                     size_t wordLength, uint8_t* output,
                                                                     size_t outputCapacity, size_t& outputCount);
  void handleTextSettingsReturn(const LayoutSettingsSnapshot& previous, size_t sourceAnchor, bool hasAnchor);

 public:
#ifdef CROSSPOINT_TXT_READER_TEST
  friend struct TxtReaderTestAccess;
#endif
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("TxtReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~TxtReaderActivity() override = default;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
