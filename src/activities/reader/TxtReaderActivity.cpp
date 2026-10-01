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
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Cache file magic and version
constexpr uint8_t CACHE_VERSION = txt::INDEX_VERSION_V5;  // Increment when cache behavior changes
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

void TxtReaderActivity::initializeReader(GfxRenderer& renderer) {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedFontPointSize = SETTINGS.fontPointSize;
  cachedScreenMargin = SETTINGS.screenMargin;
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
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  lineAdvance = txt::PageLayout::effectiveLineAdvance(
      renderer.getLineHeight(cachedFontId, SETTINGS.getReaderLineCompression()));
  linesPerPage = txt::PageLayout::calculateLinesPerPage(viewportHeight, lineAdvance);

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

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
  if (!loadPageIndexCache()) {
    // Cache not found, build page index
    buildPageIndex(renderer);
    // Save to cache for next time
    savePageIndexCache();
  }

  applySavedProgress(savedProgress, hasProgress);

  initialized = true;
}

void TxtReaderActivity::buildPageIndex(GfxRenderer& renderer) {
  pageOffsets.clear();
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();
  std::vector<txt::PageLine> tempLines;

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    size_t nextOffset = offset;

    if (!loadPageAtOffset(renderer, offset, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
}

bool TxtReaderActivity::loadPageAtOffset(const GfxRenderer& renderer, size_t offset,
                                         std::vector<txt::PageLine>& outLines,
                                         size_t& nextOffset) {
  outLines.clear();
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

  const char* textData = reinterpret_cast<const char*>(buffer.get() + window.pagePrefix);
  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, textData, /*styleMask=*/0x01);
  }

  const bool startsParagraph = offset == 0 || buffer[window.pagePrefix - 1] == '\n';
  auto textAdvance = [&renderer, this](const char* line) {
    return renderer.getTextAdvanceX(cachedFontId, line, EpdFontFamily::REGULAR);
  };
  return txt::PageLayout::loadPage(buffer.get(), window.offset, window.length, offset, fileSize, linesPerPage,
                                   viewportWidth, startsParagraph, textAdvance, outLines, nextOffset);
}

void TxtReaderActivity::renderBook() {
  if (!txt) {
    return;
  }

  if (!initialized) {
    initializeReader(renderer);
  }

  if (pageOffsets.empty()) {
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
  loadPageAtOffset(renderer, offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage(renderer);

  // Save progress
  saveProgress();
}

void TxtReaderActivity::renderPage(GfxRenderer& renderer) {
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& line : currentPageLines) {
      if (!line.text.empty()) {
        int x = cachedOrientedMarginLeft;
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.text.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        const int textWidth = renderer.getTextAdvanceX(cachedFontId, line.text.c_str(), EpdFontFamily::REGULAR);

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

        renderer.drawText(cachedFontId, x, y, line.text.c_str());
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
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data))) {
    LOG_ERR("TRS", "Failed to save progress: page %d", currentPage);
  }
}

bool TxtReaderActivity::readSavedProgress(txt::ProgressRecord& progress) const {
  HalFile f;
  if (!Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) return false;
  uint8_t data[8]{};
  const int bytesRead = f.read(data, sizeof(data));
  return bytesRead >= 4 && txt::parseProgressRecord(data, static_cast<size_t>(bytesRead),
                                                    static_cast<uint32_t>(txt->getFileSize()), progress);
}

bool TxtReaderActivity::deriveLegacySourceAnchor(uint16_t page, uint32_t& sourceAnchor) const {
  HalFile index;
  if (!Storage.openFileForRead("TRS", txt->getCachePath() + "/index.bin", index)) return false;

  uint8_t rawHeader[txt::INDEX_HEADER_V5_SIZE]{};
  const int bytesRead = index.read(rawHeader, sizeof(rawHeader));
  txt::IndexHeader header;
  if (bytesRead < 0 ||
      !txt::parseIndexHeader(rawHeader, static_cast<size_t>(bytesRead), index.size(), header) ||
      (header.version != txt::INDEX_VERSION_V3 && header.version != txt::INDEX_VERSION_V4)) {
    return false;
  }

  const size_t sourceSize = txt->getFileSize();
  if (sourceSize > UINT32_MAX) return false;
  HalFile source;
  if (!Storage.openFileForRead("TRS", bookPath, source) || source.size() != sourceSize) return false;

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
  HalFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  uint8_t rawHeader[txt::INDEX_HEADER_V5_SIZE]{};
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
                              cachedParagraphAlignment, static_cast<int32_t>(lineAdvance), cachedFontPointSize};
  if (!txt::matchesLayout(header, layout)) {
    LOG_DBG("TRS", "Cache layout mismatch, rebuilding");
    return false;
  }

  pageOffsets.clear();
  pageOffsets.reserve(header.pageCount);

  for (uint32_t i = 0; i < header.pageCount; i++) {
    uint8_t rawOffset[sizeof(uint32_t)];
    if (f.read(rawOffset, sizeof(rawOffset)) != sizeof(rawOffset)) {
      pageOffsets.clear();
      LOG_DBG("TRS", "Truncated page index, rebuilding");
      return false;
    }
    const uint32_t offset = txt::readLe32(rawOffset);
    const bool validOffset = header.sourceFileSize == 0 ? offset == 0 : offset < header.sourceFileSize;
    if (!validOffset || (i == 0 ? offset != 0 : offset <= pageOffsets.back())) {
      pageOffsets.clear();
      LOG_DBG("TRS", "Invalid page offset, rebuilding");
      return false;
    }
    pageOffsets.push_back(offset);
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void TxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  serialization::writePod(f, txt::INDEX_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<int32_t>(lineAdvance));
  serialization::writePod(f, cachedFontPointSize);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
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
