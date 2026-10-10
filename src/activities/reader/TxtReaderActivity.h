#pragma once

#include <Txt.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReaderActivity.h"
#include "TxtPageEstimate.h"
#include "TxtPageReader.h"
#include "TxtProgressFile.h"

class TxtReaderActivity final : public ReaderActivity {
  std::unique_ptr<Txt> txt;

  TxtPageHistory pageHistory;
  TxtPageReader pageReader;
  TxtPageEstimate pageEstimate;
  TxtProgressFile progressFile;
  size_t currentDisplayOrdinal = 1;
  bool hasCurrentDisplayOrdinal = false;
  TxtProgressState lastVisibleProgressState;
  bool hasVisiblePageStart = false;
  std::vector<std::string> currentPageLines;
  size_t currentPageEndOffset = 0;
  bool currentPageLoaded = false;
  bool pendingScreenshot = false;
  bool pendingInitialIndexingPopup = false;
  std::atomic<bool> endOfBookRequested{false};
  int linesPerPage = 0;
  int viewportHeight = 0;
  int lineAdvance = 1;
  int viewportWidth = 0;
  std::atomic<bool> initialized{false};

  // Settings latched for this reader session
  int cachedFontId = 0;
  uint8_t cachedLineSpacing = CrossPointSettings::NORMAL;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  uint8_t cachedOrientation = 0;
  bool cachedHyphenationEnabled = false;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage(GfxRenderer& renderer);
  void persistVisibleCheckpoint();
  void initializeReader(const GfxRenderer& renderer);
  void updateLayoutIfChanged(const GfxRenderer& renderer);
  void updateViewport(const GfxRenderer& renderer);
  void openReaderOptions();
  int estimatedTotalPages() const;
  float estimatedProgressPercent() const;
  void renderStatusBar() const;
  size_t estimatedCurrentPageNumber() const;

  bool loadBook() override;
  bool handleFormatInput() override;
  std::string getBookTitle() const override { return txt ? txt->getTitle() : ""; }
  void renderBook() override;

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("TxtReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~TxtReaderActivity() override = default;

  void onExit() override;
  bool pageTurn(bool isForward) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
