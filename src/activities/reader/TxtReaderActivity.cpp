#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Serialization.h>
#include <Utf8.h>

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#ifndef CROSSPOINT_TXT_READER_TEST
#include "ReaderActivity.h"
#endif
#ifdef CROSSPOINT_TXT_READER_TEST
#include <TxtReaderHostUtils.h>
#else
#include "ReaderUtils.h"
#endif
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Cache file magic and version
// V8 identifies the current deterministic TXT pagination policy.
constexpr uint8_t CACHE_VERSION = txt::INDEX_VERSION_V8;

txt::PageLayout::Geometry::Alignment pageAlignmentForSettings(uint8_t alignment) {
  using Alignment = txt::PageLayout::Geometry::Alignment;
  switch (alignment) {
    case CrossPointSettings::JUSTIFIED:
      return Alignment::JUSTIFIED;
    case CrossPointSettings::LEFT_ALIGN:
      return Alignment::LEFT;
    case CrossPointSettings::CENTER_ALIGN:
      return Alignment::CENTER;
    case CrossPointSettings::RIGHT_ALIGN:
      return Alignment::RIGHT;
    case CrossPointSettings::BOOK_STYLE:
      return Alignment::BOOK_STYLE;
    default:
      return Alignment::LEFT;
  }
}

bool txtLineStartsWithRtl(const char* text) {
  return BidiUtils::startsWithRtl(text, BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
}
}  // namespace

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

bool TxtReaderActivity::initializeReader(GfxRenderer& renderer) {
  if (initialized) return true;

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedFontPointSize = SETTINGS.fontPointSize;
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedExtraParagraphSpacing = SETTINGS.extraParagraphSpacing != 0;
  cachedParagraphIndentSpaces = SETTINGS.paragraphIndentSpaces;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  cachedViewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  lineAdvance = txt::PageLayout::effectiveLineAdvance(
      renderer.getLineHeight(cachedFontId, SETTINGS.getReaderLineCompression()));
  paragraphGap = cachedExtraParagraphSpacing ? lineAdvance / 2 : 0;
  if (!metricContext) {
    metricContext = makeUniqueNoThrow<SdCardFont::TxtMetricContext>();
    if (!metricContext) {
      LOG_ERR("TRS", "Failed to allocate TXT metric context");
      return false;
    }
    bodyTypography.metrics = metricContext.get();
  }
  const auto font = renderer.getSdCardFonts().find(cachedFontId);
  if (font != renderer.getSdCardFonts().end()) {
    lastTextResult.metricStatus = font->second->openTxtMetrics(*metricContext);
    if (lastTextResult.metricStatus != SdCardFont::TxtMetricStatus::Ok) {
      LOG_ERR("TRS", "Failed to open TXT metric metadata");
      return false;
    }
  }
  lastTextResult = renderer.measureTextWithTypography(cachedFontId, " ", bodyTypography);
  if (!lastTextResult.ok()) {
    LOG_ERR("TRS", "Failed to measure TXT indentation space");
    return false;
  }
  paragraphIndentWidth = txt::PageLayout::indentWidthForSpaces(lastTextResult.advance, cachedParagraphIndentSpaces);
  linesPerPage = txt::PageLayout::calculateLinesPerPage(cachedViewportHeight, lineAdvance);

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, cachedViewportHeight, linesPerPage);

  // Migrate old page-only progress before an incompatible index can be rebuilt.
  txt::ProgressRecord savedProgress;
  const bool hasProgress = readSavedProgress(savedProgress);
  if (hasProgress && !savedProgress.hasAnchorRecord) {
    uint32_t legacyAnchor = 0;
    if (deriveLegacySourceAnchor(savedProgress.page, legacyAnchor)) {
      savedProgress.sourceAnchor = legacyAnchor;
      savedProgress.hasSourceAnchor = true;
      LOG_DBG("TRS", "Migrated legacy page %u to source offset %u", savedProgress.page, legacyAnchor);
    }
  }

  // Try to load cached page index first
  if (cacheRejected || !loadPageIndexCache()) {
    // Cache not found, build page index
    if (!buildPageIndex(renderer)) return false;
    cacheRejected = false;
    // A complete in-memory index remains usable if persistence fails.
    savePageIndexCache();
  }

  applySavedProgress(savedProgress, hasProgress);

  initialized = true;
  return true;
}

bool TxtReaderActivity::buildPageIndex(GfxRenderer& renderer) {
  txt::PageOffsets candidateOffsets;
  if (!candidateOffsets.reserve(std::max<size_t>(pageOffsets.size(), 16)) || !candidateOffsets.push_back(0)) {
    LOG_ERR("TRS", "Failed to allocate TXT page index");
    return false;
  }

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();
  if (fileSize > UINT32_MAX) {
    LOG_ERR("TRS", "TXT source exceeds index offset range");
    return false;
  }
  std::vector<txt::PageLine> tempLines;

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    size_t nextOffset = offset;

    if (!loadPageAtOffset(renderer, offset, tempLines, nextOffset)) {
      LOG_ERR("TRS", "TXT index failed at offset %zu", offset);
      return false;
    }

    if (nextOffset <= offset) {
      LOG_ERR("TRS", "TXT index made no progress at %zu", offset);
      return false;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      if (!candidateOffsets.push_back(offset)) {
        LOG_ERR("TRS", "Failed to grow TXT page index");
        return false;
      }
    }

    // Yield to other tasks periodically
    if (candidateOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  pageOffsets.swap(candidateOffsets);
  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
  return true;
}

bool TxtReaderActivity::loadPageAtOffset(const GfxRenderer& renderer, size_t offset,
                                         std::vector<txt::PageLine>& outLines,
                                         size_t& nextOffset) {
  outLines.clear();
  nextOffset = offset;
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  const auto window = txt::PageLayout::readWindow(offset, fileSize);
  auto buffer = makeUniqueNoThrow<uint8_t[]>(window.length + 1);
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", window.length);
    return false;
  }

  if (!txt->readContent(buffer.get(), window.offset, window.length)) return false;
  buffer[window.length] = '\0';

  const bool startsParagraph = offset == 0 || buffer[window.pagePrefix - 1] == '\n';
  auto textAdvance = [&renderer, this](const char* line) {
    lastTextResult = renderer.measureTextWithTypography(cachedFontId, line, bodyTypography);
    return lastTextResult.ok() ? lastTextResult.advance : -1;
  };
  const txt::PageLayout::Geometry geometry{cachedViewportHeight,
                                           lineAdvance,
                                           paragraphGap,
                                           paragraphIndentWidth,
                                           pageAlignmentForSettings(cachedParagraphAlignment),
                                           txtLineStartsWithRtl};
  return txt::PageLayout::loadPage(buffer.get(), window.offset, window.length, offset, fileSize, linesPerPage,
                                   viewportWidth, startsParagraph, textAdvance, outLines, nextOffset, geometry);
}

void TxtReaderActivity::renderBook() {
  if (!txt) {
    return;
  }

  if (!initialized) {
    if (!initializeReader(renderer)) return;
  }

  if (pageOffsets.empty() || txt->getFileSize() == 0) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  if (!loadPageAtOffset(renderer, offset, currentPageLines, nextOffset) ||
      nextOffset != (currentPage + 1 < totalPages ? pageOffsets[currentPage + 1] : txt->getFileSize())) {
    LOG_ERR("TRS", "TXT page reconstruction failed or disagreed with index");
    initialized = false;
    cacheRejected = true;
    return;
  }

  renderer.clearScreen();
  if (!renderPage(renderer)) return;

  // Save progress
  saveProgress();
}

bool TxtReaderActivity::renderPage(GfxRenderer& renderer) {
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    bool firstLineOnPage = true;
    for (size_t lineIndex = 0; lineIndex < currentPageLines.size(); ++lineIndex) {
      const auto& line = currentPageLines[lineIndex];
      y += txt::PageLayout::gapBeforeLine(line, firstLineOnPage, paragraphGap);
      if (!line.text.empty()) {
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.text.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        const auto paragraphAlignment = pageAlignmentForSettings(cachedParagraphAlignment);
        const int indent = txt::PageLayout::indentForLine(line, paragraphIndentWidth, paragraphAlignment, lineIsRtl);
        const int availableWidth = contentWidth - indent;
        int x = cachedOrientedMarginLeft + indent;
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        lastTextResult = renderer.measureTextWithTypography(cachedFontId, line.text.c_str(), bodyTypography);
        if (!lastTextResult.ok()) return false;
        const int textWidth = lastTextResult.advance;
        int8_t tracking = 0;
        const bool justifyLine = txt::PageLayout::shouldJustifyLine(
            line, lineIndex + 1 == currentPageLines.size(), paragraphAlignment, lineIsRtl);
        if (justifyLine) {
          bool trackingFailed = false;
          const auto measureWithTracking = [&renderer, this, &line, &trackingFailed](int value) {
            lastTextResult = renderer.measureTextWithTypography(cachedFontId, line.text.c_str(), bodyTypography,
                                                                EpdFontFamily::REGULAR, static_cast<int8_t>(value));
            trackingFailed = trackingFailed || !lastTextResult.ok();
            return lastTextResult.ok() ? lastTextResult.advance : -1;
          };
          const int boundaryCount = txt::PageLayout::trackingBoundaryCount(measureWithTracking);
          const int renderedTextWidth = measureWithTracking(0);
          tracking = static_cast<int8_t>(
              txt::PageLayout::justifiedTracking(availableWidth, renderedTextWidth, boundaryCount));
          if (trackingFailed) return false;
          while (tracking > 0 && measureWithTracking(tracking) > availableWidth) --tracking;
          if (trackingFailed) return false;
        }

        // Apply text alignment
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            x = cachedOrientedMarginLeft + (availableWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            x = cachedOrientedMarginLeft + availableWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            break;
        }

        lastTextResult = renderer.drawTextWithTypography(cachedFontId, x, y, line.text.c_str(), bodyTypography,
                                                        true, EpdFontFamily::REGULAR, BidiUtils::BidiBaseDir::AUTO, tracking);
        if (!lastTextResult.ok()) return false;
      }
      y += lineAdvance;
      firstLineOnPage = false;
    }
    return true;
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  if (!renderLines()) return false;  // scan pass
  renderStatusBar();  // scan: a CJK title joins the batch prewarm
  scope.endScanAndPrewarm();

  // BW rendering
  if (!renderLines()) return false;
  renderStatusBar();

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
    bool rendered = true;
    ReaderUtils::renderAntiAliased(renderer, [&renderLines, &rendered]() { rendered = renderLines() && rendered; });
    if (!rendered) return false;
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
  return true;
}

void TxtReaderActivity::renderStatusBar() const {
  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  std::string title;
  if (SETTINGS.statusBarSpec().showsTitle()) {
    title = txt->getTitle();
  }
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title);
}

bool TxtReaderActivity::pageTurn(bool isForward) {
  // Ignore paging until initializeReader has established the page index
  if (!initialized) {
    return false;
  }
  if (isForward) {
    if (currentPage < totalPages) {
      currentPage++;
      return true;
    }
  } else {
    if (currentPage > 0) {
      currentPage--;
      return true;
    }
  }
  return false;
}

bool TxtReaderActivity::skipPages(int amount) {
  if (!initialized) {
    return false;
  }
  int newPage = currentPage + amount;
  if (newPage < 0) newPage = 0;
  // Clamp to totalPages, not totalPages - 1: pageTurn() lets currentPage reach
  // totalPages and isAtEndOfBook() treats that as the end-of-book sentinel, so
  // a forward skip must be able to reach it too.
  if (newPage > totalPages) newPage = totalPages;
  if (newPage != currentPage) {
    currentPage = newPage;
    return true;
  }
  return false;
}

bool TxtReaderActivity::isAtEndOfBook() const { return initialized && currentPage >= totalPages; }

void TxtReaderActivity::onReturnFromEndOfBook() { currentPage = totalPages > 0 ? totalPages - 1 : 0; }

void TxtReaderActivity::saveProgress() const {
  uint8_t data[8];
  const size_t fileSize = txt->getFileSize();
  const size_t sourceAnchor = currentPage >= 0 && currentPage < static_cast<int>(pageOffsets.size())
                                  ? pageOffsets[currentPage]
                                  : fileSize;
  txt::writeProgressRecord(data, static_cast<uint16_t>(currentPage), static_cast<uint32_t>(sourceAnchor));
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data), true)) {
    LOG_ERR("TRS", "Failed to save progress: page %d", currentPage);
  }
}

bool TxtReaderActivity::readSavedProgress(txt::ProgressRecord& progress) const {
  const std::string progressPath = txt->getCachePath() + "/progress.bin";
  if (!Storage.exists(progressPath.c_str())) {
    const std::string backupPath = progressPath + ".bak";
    if (Storage.exists(backupPath.c_str())) Storage.rename(backupPath.c_str(), progressPath.c_str());
  }
  HalFile f;
  if (Storage.openFileForReadChecked("TRS", (txt->getCachePath() + "/progress.bin").c_str(), f) != HalStorage::ReadOpenResult::Ok) return false;
  uint8_t data[8]{};
  const int bytesRead = f.read(data, sizeof(data));
  return bytesRead >= 4 && txt::parseProgressRecord(data, static_cast<size_t>(bytesRead),
                                                    static_cast<uint32_t>(txt->getFileSize()), progress);
}

bool TxtReaderActivity::deriveLegacySourceAnchor(uint16_t page, uint32_t& sourceAnchor) const {
  HalFile index;
  if (Storage.openFileForReadChecked("TRS", (txt->getCachePath() + "/index.bin").c_str(), index) != HalStorage::ReadOpenResult::Ok) return false;

  uint8_t rawHeader[txt::INDEX_HEADER_CURRENT_SIZE]{};
  const int bytesRead = index.read(rawHeader, sizeof(rawHeader));
  txt::IndexHeader header;
  if (bytesRead < 0 || !txt::parseIndexHeader(rawHeader, static_cast<size_t>(bytesRead), index.size(), header)) {
    return false;
  }

  const size_t sourceSize = txt->getFileSize();
  if (sourceSize > UINT32_MAX) return false;
  HalFile source;
  if (Storage.openFileForReadChecked("TRS", bookPath.c_str(), source) != HalStorage::ReadOpenResult::Ok || source.size() != sourceSize) return false;

  auto readPageOffset = [&index, &header](uint32_t page, uint32_t& value) {
    const size_t position = header.headerSize + static_cast<size_t>(page) * sizeof(uint32_t);
    uint8_t bytes[4];
    if (!index.seek(position) || index.read(bytes, sizeof(bytes)) != sizeof(bytes)) return false;
    value = txt::readLe32(bytes);
    return true;
  };
  auto isUtf8Boundary = [&source, sourceSize](uint32_t offset) {
    if (offset == sourceSize) return true;
    uint8_t byte = 0;
    return source.seek(offset) && source.read(&byte, 1) == 1 && (byte & 0xC0) != 0x80;
  };
  return txt::deriveLegacyPageAnchor(header, page, static_cast<uint32_t>(sourceSize), readPageOffset,
                                     isUtf8Boundary, sourceAnchor);
}

void TxtReaderActivity::applySavedProgress(const txt::ProgressRecord& progress, bool hasProgress) {
  if (!hasProgress || totalPages <= 0) {
    currentPage = 0;
    return;
  }

  if (progress.hasSourceAnchor && !pageOffsets.empty()) {
    currentPage = static_cast<int>(txt::PageLayout::pageForSourceAnchor(pageOffsets, progress.sourceAnchor));
    if (currentPage >= totalPages) currentPage = totalPages - 1;
    LOG_DBG("TRS", "Restored source offset %u at page %d/%d", progress.sourceAnchor, currentPage + 1, totalPages);
    return;
  }

  currentPage = std::min<int>(progress.page, totalPages - 1);
  LOG_DBG("TRS", "Restored saved page %u approximately at page %d/%d", progress.page, currentPage + 1, totalPages);
}

bool TxtReaderActivity::loadPageIndexCache() {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  if (!Storage.exists(cachePath.c_str())) {
    const std::string backupPath = cachePath + ".bak";
    if (Storage.exists(backupPath.c_str())) Storage.rename(backupPath.c_str(), cachePath.c_str());
  }
  HalFile f;
  if (Storage.openFileForReadChecked("TRS", cachePath.c_str(), f) != HalStorage::ReadOpenResult::Ok) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  uint8_t rawHeader[txt::INDEX_HEADER_CURRENT_SIZE]{};
  const int bytesRead = f.read(rawHeader, sizeof(rawHeader));
  txt::IndexHeader header;
  if (bytesRead < 0 || !txt::parseIndexHeader(rawHeader, static_cast<size_t>(bytesRead), f.size(), header)) {
    LOG_DBG("TRS", "Invalid page index header, rebuilding");
    return false;
  }
  if (header.version != CACHE_VERSION || header.sourceFileSize != txt->getFileSize()) {
    LOG_DBG("TRS", "Cache version or file size mismatch, rebuilding");
    return false;
  }

  const txt::LayoutKey layout{static_cast<int32_t>(viewportWidth), static_cast<int32_t>(linesPerPage),
                              static_cast<int32_t>(cachedFontId), static_cast<int32_t>(cachedScreenMargin),
                              cachedParagraphAlignment, static_cast<int32_t>(lineAdvance), cachedFontPointSize,
                              static_cast<int32_t>(cachedViewportHeight), static_cast<int32_t>(paragraphGap),
                              static_cast<int32_t>(paragraphIndentWidth), cachedExtraParagraphSpacing,
                              cachedParagraphIndentSpaces};
  if (!txt::matchesLayout(header, layout)) {
    LOG_DBG("TRS", "Cache layout mismatch, rebuilding");
    return false;
  }

  txt::PageOffsets candidateOffsets;
  if (!candidateOffsets.reserve(header.pageCount)) return false;

  for (uint32_t i = 0; i < header.pageCount; i++) {
    uint8_t rawOffset[sizeof(uint32_t)];
    if (f.read(rawOffset, sizeof(rawOffset)) != sizeof(rawOffset)) {
      LOG_DBG("TRS", "Truncated page index, rebuilding");
      return false;
    }
    const uint32_t offset = txt::readLe32(rawOffset);
    const bool validOffset = header.sourceFileSize == 0 ? offset == 0 : offset < header.sourceFileSize;
    if (!validOffset || (i == 0 ? offset != 0 : offset <= candidateOffsets.back())) {
      LOG_DBG("TRS", "Invalid page offset, rebuilding");
      return false;
    }
    if (!candidateOffsets.push_back(offset)) return false;
  }

  pageOffsets.swap(candidateOffsets);
  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

bool TxtReaderActivity::savePageIndexCache() const {
  const std::string cachePath = txt->getCachePath() + "/index.bin";
  const std::string temporaryPath = cachePath + ".tmp";
  const std::string backupPath = cachePath + ".bak";
  {
    HalFile file;
    if (Storage.openFileForWriteChecked("TRS", temporaryPath.c_str(), file) != HalStorage::ReadOpenResult::Ok) return false;
    uint8_t header[txt::INDEX_HEADER_CURRENT_SIZE]{};
    txt::writeLe32(header, txt::INDEX_MAGIC);
    header[4] = CACHE_VERSION;
    txt::writeLe32(header + 5, static_cast<uint32_t>(txt->getFileSize()));
    txt::writeLe32(header + 9, viewportWidth);
    txt::writeLe32(header + 13, linesPerPage);
    txt::writeLe32(header + 17, cachedFontId);
    txt::writeLe32(header + 21, cachedScreenMargin);
    header[25] = cachedParagraphAlignment;
    txt::writeLe32(header + 26, lineAdvance);
    header[30] = cachedFontPointSize;
    txt::writeLe32(header + 31, cachedViewportHeight);
    txt::writeLe32(header + 35, paragraphGap);
    txt::writeLe32(header + 39, paragraphIndentWidth);
    header[43] = cachedExtraParagraphSpacing;
    header[44] = cachedParagraphIndentSpaces;
    txt::writeLe32(header + 45, static_cast<uint32_t>(pageOffsets.size()));
    if (file.write(header, sizeof(header)) != sizeof(header)) return false;
    for (size_t offset : pageOffsets) {
      uint8_t bytes[4];
      txt::writeLe32(bytes, static_cast<uint32_t>(offset));
      if (file.write(bytes, sizeof(bytes)) != sizeof(bytes)) return false;
    }
    file.flush();
  }
  const bool hadIndex = Storage.exists(cachePath.c_str());
  if (hadIndex) {
    if (Storage.exists(backupPath.c_str()) && !Storage.remove(backupPath.c_str())) return false;
    if (!Storage.rename(cachePath.c_str(), backupPath.c_str())) return false;
  }
  if (!Storage.rename(temporaryPath.c_str(), cachePath.c_str())) {
    if (hadIndex) Storage.rename(backupPath.c_str(), cachePath.c_str());
    LOG_ERR("TRS", "Failed to publish TXT page index");
    return false;
  }
  if (hadIndex) Storage.remove(backupPath.c_str());
  return true;
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
