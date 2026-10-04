#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <iterator>
#include <limits>

#include "CrossPointSettings.h"
#include "EpubReaderMenuActivity.h"
#include "QrDisplayActivity.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "TxtLayoutInvalidation.h"
#include "TxtPageMetrics.h"
#include "activities/settings/TextSettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"

bool TxtReaderActivity::loadBook() {
  txt = makeUniqueNoThrow<Txt>(bookPath, "/.crosspoint");
  if (!txt) {
    LOG_ERR("TRS", "Failed to allocate TXT object");
    return false;
  }
  if (!txt->load()) {
    LOG_ERR("TRS", "Failed to load TXT");
    return false;
  }
  txt->setupCacheDir();
  return true;
}

bool TxtReaderActivity::handleFormatInput() {
  if (isAtEndOfBook()) return false;

  if (mappedInput.homeButtonAction() != HomeButtonAction::ReaderMenu &&
      !mappedInput.wasReleased(MappedInputManager::Button::Confirm) &&
      !ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
    return false;
  }

  openReaderOptions();
  return true;
}

void TxtReaderActivity::openReaderOptions() {
  if (!initialized) {
    RenderLock lock(*this);
    if (!initialized) {
      initializeReader(renderer);
      pendingInitialIndexingPopup = initialized && txt && txt->getFileSize() > 0;
    }
  }

  const int currentPage = static_cast<int>(std::min<size_t>(estimatedCurrentPageNumber(), INT_MAX));
  const int progress = static_cast<int>(estimatedProgressPercent() + 0.5f);
  startActivityForResult(
      std::make_unique<EpubReaderMenuActivity>(renderer, mappedInput, getBookTitle(), currentPage,
                                               estimatedTotalPages(), progress, SETTINGS.orientation, false, false,
                                               readermenu::Profile::Txt),
      [this](const ActivityResult& result) {
        const auto& menu = std::get<MenuResult>(result.data);
        {
          RenderLock lock(*this);
          if (SETTINGS.orientation != menu.orientation) {
            SETTINGS.orientation = menu.orientation;
            SETTINGS.saveToFile();
            ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
          }
          updateLayoutIfChanged(renderer);
        }

        if (result.isCancelled) {
          requestUpdate();
          return;
        }

        switch (static_cast<EpubReaderMenuActivity::MenuAction>(menu.action)) {
          case EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS:
            startActivityForResult(
                std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                       TextSettingsActivity::Tab::Family, textsettings::Profile::Txt),
                [this](const ActivityResult&) {
                  {
                    RenderLock lock(*this);
                    updateLayoutIfChanged(renderer);
                  }
                  openReaderOptions();
                });
            break;
          case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
            {
              RenderLock lock(*this);
              pendingScreenshot = true;
            }
            requestUpdate();
            break;
          }
          case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
            std::string pageText;
            {
              RenderLock lock(*this);
              if (currentPageLoaded) {
                size_t needed = currentPageLines.empty() ? 0 : currentPageLines.size() - 1;
                for (const auto& line : currentPageLines) needed += line.size();
                pageText.reserve(needed);
                for (size_t i = 0; i < currentPageLines.size(); ++i) {
                  if (i > 0) pageText.push_back('\n');
                  pageText += currentPageLines[i];
                }
              }
            }
            if (pageText.empty()) {
              requestUpdate();
            } else {
              startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, pageText),
                                     [this](const ActivityResult&) { openReaderOptions(); });
            }
            break;
          }
          case EpubReaderMenuActivity::MenuAction::GO_HOME:
            onGoHome();
            break;
          case EpubReaderMenuActivity::MenuAction::NIGHT_MODE:
          case EpubReaderMenuActivity::MenuAction::FRONTLIGHT:
          case EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN:
            requestUpdate();
            break;
          default:
            requestUpdate();
            break;
        }
      });
}

void TxtReaderActivity::initializeReader(GfxRenderer& renderer) {
  if (initialized) {
    return;
  }

  // Latch the layout settings for this reader session.
  cachedFontId = SETTINGS.getReaderFontId();
  cachedLineSpacing = SETTINGS.lineSpacing;
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;
  cachedOrientation = SETTINGS.orientation;
  cachedHyphenationEnabled = SETTINGS.hyphenationEnabled != 0;

  updateViewport(renderer);

  // Known issue: progress records have no line-spacing identity, so changing
  // the global setting while TXT is closed can restore a stale ordinal/history.
  TxtProgressState restoredState;
  progressFile.load(*txt, restoredState, cachedHyphenationEnabled);
  if (!pageHistory.resetWithPrevious(restoredState.previousPageStarts, restoredState.previousPageCount,
                                     restoredState.pageStartOffset)) {
    pageHistory.reset(restoredState.pageStartOffset);
  }
  pageEstimate.reset();
  currentDisplayOrdinal = restoredState.displayOrdinal;
  hasCurrentDisplayOrdinal = restoredState.hasDisplayOrdinal;
  lastVisibleProgressState = {};
  hasVisiblePageStart = false;
  currentPageEndOffset = 0;
  currentPageLoaded = false;
  endOfBookRequested = false;
  currentPageLines.clear();
  currentPageLines.reserve(static_cast<size_t>(linesPerPage));
  initialized = true;
}

void TxtReaderActivity::updateViewport(GfxRenderer& renderer) {
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  lineAdvance =
      TxtPageMetrics::effectiveLineAdvance(renderer.getLineHeight(cachedFontId, SETTINGS.getReaderLineCompression()));
  linesPerPage = TxtPageMetrics::linesPerPage(viewportHeight, lineAdvance);

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);
}

void TxtReaderActivity::renderBook() {
  if (!txt) {
    return;
  }

  if (!initialized) {
    initializeReader(renderer);
    if (initialized && txt->getFileSize() > 0) GUI.drawPopup(renderer, tr(STR_INDEXING));
  }

  if (pendingInitialIndexingPopup) {
    pendingInitialIndexingPopup = false;
    if (txt->getFileSize() > 0) GUI.drawPopup(renderer, tr(STR_INDEXING));
  }

  updateLayoutIfChanged(renderer);

  if (txt->getFileSize() == 0) {
    currentPageLoaded = true;
    currentPageEndOffset = 0;
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    persistVisibleCheckpoint();
    if (pendingScreenshot) {
      pendingScreenshot = false;
      ScreenshotUtil::takeScreenshot(renderer);
    }
    return;
  }

  if (!currentPageLoaded) {
    const size_t pageStartOffset = pageHistory.currentOffset();
    if (!pageReader.loadPage(*txt, renderer, cachedFontId, viewportWidth, linesPerPage, pageStartOffset,
                             currentPageLines, currentPageEndOffset, cachedHyphenationEnabled) ||
        currentPageEndOffset <= pageStartOffset) {
      LOG_ERR("TRS", "Failed to load TXT page at byte %zu", pageStartOffset);
      renderer.clearScreen();
      renderer.displayBuffer();
      return;
    }
    pageEstimate.observePage(pageStartOffset, currentPageEndOffset);
    if (!hasCurrentDisplayOrdinal) {
      currentDisplayOrdinal = pageEstimate.pageNumberAt(pageStartOffset);
      hasCurrentDisplayOrdinal = true;
    }
    currentPageLoaded = true;
  }

  renderer.clearScreen();
  renderPage(renderer);
  persistVisibleCheckpoint();
  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
}

void TxtReaderActivity::updateLayoutIfChanged(GfxRenderer& renderer) {
  if (!initialized) return;

  const size_t pageStartOffset = pageHistory.currentOffset();
  const int currentFontId = SETTINGS.getReaderFontId();
  const bool mustReflow = cachedFontId != currentFontId || cachedLineSpacing != SETTINGS.lineSpacing ||
                          cachedScreenMargin != SETTINGS.screenMargin || cachedOrientation != SETTINGS.orientation ||
                          cachedHyphenationEnabled != (SETTINGS.hyphenationEnabled != 0);
  cachedFontId = currentFontId;
  cachedLineSpacing = SETTINGS.lineSpacing;
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;
  cachedOrientation = SETTINGS.orientation;
  cachedHyphenationEnabled = SETTINGS.hyphenationEnabled != 0;
  if (!mustReflow) return;

  // Font, line-spacing, margin, and orientation changes can alter the source
  // bytes that fit on each page. Rebuild from the visible byte anchor and
  // discard old layout history so progress remains source-based.
  updateViewport(renderer);
  const TxtProgressState resetState = txtlayout::invalidateAtVisibleAnchor(
      pageHistory, pageEstimate, pageStartOffset, currentDisplayOrdinal, hasCurrentDisplayOrdinal);
  currentPageEndOffset = pageStartOffset;
  currentPageLoaded = false;
  currentPageLines.clear();
  currentPageLines.reserve(static_cast<size_t>(linesPerPage));
  endOfBookRequested = false;

  // Clear the persisted three-page micro-history and old display ordinal at
  // the same source anchor. The page estimate will be rebuilt from this page.
  lastVisibleProgressState = resetState;
  hasVisiblePageStart = true;
  if (!progressFile.save(*txt, resetState, cachedHyphenationEnabled)) {
    LOG_ERR("TRS", "Failed to reset TXT layout history at byte %zu", pageStartOffset);
  }
}

void TxtReaderActivity::renderPage(GfxRenderer& renderer) {
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& line : currentPageLines) {
      if (!line.empty()) {
        int x = cachedOrientedMarginLeft;
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        const int textWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR);

        // Apply text alignment
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            x = cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            break;
        }

        renderer.drawText(cachedFontId, x, y, line.c_str());
      }
      y += lineAdvance;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();      // scan pass
  renderStatusBar();  // scan: a CJK title joins the batch prewarm
  scope.endScanAndPrewarm();

  // BW rendering
  renderLines();
  renderStatusBar();

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
}

void TxtReaderActivity::persistVisibleCheckpoint() {
  if (!txt) return;

  TxtProgressState state;
  state.pageStartOffset = pageHistory.currentOffset();
  state.hasDisplayOrdinal = hasCurrentDisplayOrdinal;
  state.displayOrdinal = currentDisplayOrdinal;
  state.previousPageCount = pageHistory.copyPreviousOffsets(state.previousPageStarts);
  lastVisibleProgressState = state;
  hasVisiblePageStart = true;
  if (!progressFile.save(*txt, state, cachedHyphenationEnabled)) {
    LOG_ERR("TRS", "Failed to save TXT position at byte %zu", state.pageStartOffset);
    return;
  }
}

void TxtReaderActivity::onExit() {
  {
    // A changed layout may reach Home before another render can invalidate the
    // old pagination history. ActivityManager enters onExit with RenderLock
    // already held, so this scope must not acquire the non-recursive lock again.
    if (txt && initialized) updateLayoutIfChanged(renderer);
  }
  if (txt && hasVisiblePageStart && !progressFile.save(*txt, lastVisibleProgressState, cachedHyphenationEnabled)) {
    LOG_ERR("TRS", "Failed to save TXT position on exit at byte %zu", lastVisibleProgressState.pageStartOffset);
  }
  ReaderActivity::onExit();
}

void TxtReaderActivity::renderStatusBar() const {
  std::string title;
  if (SETTINGS.statusBarSpec().showsTitle()) {
    title = txt->getTitle();
  }
  const int currentPageNumber = static_cast<int>(std::min<size_t>(estimatedCurrentPageNumber(), INT_MAX));
  GUI.drawStatusBar(renderer, estimatedProgressPercent(), currentPageNumber, estimatedTotalPages(), title, 0, 0, true,
                    false, true);
}

int TxtReaderActivity::estimatedTotalPages() const {
  if (!txt || txt->getFileSize() == 0) return 0;

  const size_t currentPageNumber = estimatedCurrentPageNumber();
  const size_t visiblePageOffset = currentPageLoaded ? currentPageEndOffset : pageHistory.currentOffset();
  const size_t totalPages =
      pageEstimate.totalPagesFrom(currentPageNumber, txt->getFileSize(), visiblePageOffset, currentPageLoaded);
  return static_cast<int>(std::min<size_t>(totalPages, INT_MAX));
}

size_t TxtReaderActivity::estimatedCurrentPageNumber() const {
  return hasCurrentDisplayOrdinal ? currentDisplayOrdinal : pageEstimate.pageNumberAt(pageHistory.currentOffset());
}

float TxtReaderActivity::estimatedProgressPercent() const {
  if (!txt || txt->getFileSize() == 0 || !currentPageLoaded) return 0;
  return std::min(100.0f, static_cast<float>(currentPageEndOffset) * 100.0f / static_cast<float>(txt->getFileSize()));
}

bool TxtReaderActivity::pageTurn(bool isForward) {
  RenderLock lock(*this);
  if (!initialized || endOfBookRequested) return false;
  if (isForward) {
    if (!currentPageLoaded) return false;
    if (pageHistory.hasForward()) {
      if (!pageHistory.forward()) return false;
    } else {
      if (currentPageEndOffset >= txt->getFileSize()) {
        endOfBookRequested = true;
        return true;
      }
      if (!pageHistory.advance(currentPageEndOffset)) return false;
    }
    if (hasCurrentDisplayOrdinal && currentDisplayOrdinal < std::numeric_limits<size_t>::max()) {
      ++currentDisplayOrdinal;
    }
  } else {
    if (!pageHistory.retreat()) return false;
    if (hasCurrentDisplayOrdinal && currentDisplayOrdinal > 1) --currentDisplayOrdinal;
  }
  currentPageLoaded = false;
  currentPageLines.clear();
  return true;
}

bool TxtReaderActivity::isAtEndOfBook() const {
  return initialized.load(std::memory_order_acquire) && endOfBookRequested.load(std::memory_order_acquire);
}

void TxtReaderActivity::onReturnFromEndOfBook() {
  RenderLock lock(*this);
  endOfBookRequested = false;
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = static_cast<int>(std::min<size_t>(estimatedCurrentPageNumber(), INT_MAX));
  info.totalPages = estimatedTotalPages();
  info.progressPercent = static_cast<int>(estimatedProgressPercent() + 0.5f);
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
