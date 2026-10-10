#include "FontSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HardwareSerial.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "FontManager.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* DEFAULT_FONT_NAME = "KoPub 바탕 (기본)";
constexpr const char* CACHE_DIR = "/.crosspoint/cache";

// Index of `path` in `fontFiles`, or 0 (the built-in default entry) when it is not listed.
int indexOfFont(const std::vector<std::string>& fontFiles, const char* path) {
  if (fontFiles.size() < 2) return 0;
  const auto it =
      std::find_if(fontFiles.begin() + 1, fontFiles.end(), [path](const std::string& file) { return file == path; });
  return it == fontFiles.end() ? 0 : static_cast<int>(it - fontFiles.begin());
}

// Recursively delete a directory and its contents
void deleteDirectory(const char* path) {
  HalFile dir = Storage.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    char entryName[64];
    entry.getName(entryName, sizeof(entryName));
    bool entryIsDir = entry.isDirectory();
    entry.close();

    std::string fullPath = std::string(path) + "/" + entryName;
    if (entryIsDir) {
      deleteDirectory(fullPath.c_str());
    } else {
      Storage.remove(fullPath.c_str());
    }
  }
  dir.close();
  Storage.rmdir(path);
}

// Invalidate rendering caches for EPUB and TXT readers
// Keeps progress.bin (reading position) but removes layout caches
void invalidateReaderCaches() {
  LOG_DBG("FNT", "Invalidating reader rendering caches...");

  HalFile cacheDir = Storage.open(CACHE_DIR);
  if (!cacheDir || !cacheDir.isDirectory()) {
    if (cacheDir) cacheDir.close();
    LOG_DBG("FNT", "No cache directory found");
    return;
  }

  int deletedCount = 0;
  while (true) {
    HalFile bookCache = cacheDir.openNextFile();
    if (!bookCache) break;
    char bookCacheName[64];
    bookCache.getName(bookCacheName, sizeof(bookCacheName));
    bookCache.close();

    std::string bookCachePath = std::string(CACHE_DIR) + "/" + bookCacheName;

    // For EPUB: delete sections/ folder (keeps progress.bin)
    std::string sectionsPath = bookCachePath + "/sections";
    HalFile sectionsDir = Storage.open(sectionsPath.c_str());
    if (sectionsDir && sectionsDir.isDirectory()) {
      sectionsDir.close();
      deleteDirectory(sectionsPath.c_str());
      LOG_DBG("FNT", "Deleted EPUB sections cache: %s", sectionsPath.c_str());
      deletedCount++;
    } else {
      if (sectionsDir) sectionsDir.close();
    }

    // For TXT: delete index.bin (keeps progress.bin)
    std::string indexPath = bookCachePath + "/index.bin";
    if (Storage.exists(indexPath.c_str())) {
      Storage.remove(indexPath.c_str());
      LOG_DBG("FNT", "Deleted TXT index cache: %s", indexPath.c_str());
      deletedCount++;
    }
  }
  cacheDir.close();

  LOG_DBG("FNT", "Invalidated %d cache entries", deletedCount);
}
}  // namespace

void FontSelectionActivity::taskTrampoline(void* param) {
  auto* self = static_cast<FontSelectionActivity*>(param);
  self->displayTaskLoop();
}

void FontSelectionActivity::scanFontsInDirectory(const char* dirPath, bool recurseIntoSubdirs) {
  HalFile dir = Storage.open(dirPath);
  if (!dir) {
    LOG_DBG("FNT", "Font folder %s not found", dirPath);
    return;
  }

  if (!dir.isDirectory()) {
    LOG_DBG("FNT", "%s is not a directory", dirPath);
    dir.close();
    return;
  }

  // Scan for .epdfont files here, and one level into family subfolders. The per-family
  // layout places each file at /fonts/<Family>/<Family>_<size>.epdfont, so we must look
  // inside per-family subfolders; flat files directly in dirPath are also accepted.
  while (true) {
    HalFile file = dir.openNextFile();
    if (!file) break;
    char name[64];
    file.getName(name, sizeof(name));
    const bool isDir = file.isDirectory();
    file.close();  // close before recursing/continuing (serialize SD handle use)

    if (isDir) {
      // Recurse exactly one level into a family subfolder. Skip hidden/system entries.
      if (recurseIntoSubdirs && name[0] != '.' && strncmp(name, "._", 2) != 0) {
        std::string subPath = std::string(dirPath) + "/" + name;
        scanFontsInDirectory(subPath.c_str(), false);
      }
      continue;
    }

    // Accept .epdfont files (the format SdFont::load reads); skip macOS hidden files (._*)
    const size_t len = strlen(name);
    if (len > 8 && strcasecmp(name + len - 8, ".epdfont") == 0 && strncmp(name, "._", 2) != 0) {
      std::string fullPath = std::string(dirPath) + "/" + name;
      fontFiles.push_back(fullPath);
      fontNames.push_back(std::string(name, len - 8));  // display name without extension
      LOG_DBG("FNT", "Found font: %s", fullPath.c_str());
    }
  }
  dir.close();
}

void FontSelectionActivity::loadFontList() {
  fontFiles.clear();
  fontNames.clear();

  // First entry is always the default font (empty path means default).
  // Reader default is KoPub Batang; UI system-font default is Pretendard.
  fontFiles.emplace_back("");
  fontNames.emplace_back(isSystemTarget() ? "Pretendard (기본)" : DEFAULT_FONT_NAME);

  // Ensure fonts directory exists
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(FONTS_DIR);

  // Scan /.crosspoint/fonts, the visible /fonts root, and the hidden /.fonts root.
  // Each scan also descends one level into per-family subfolders (the layout:
  // /fonts/<Family>/<Family>_<size>.epdfont, or /.fonts/<Family>/... when hidden).
  scanFontsInDirectory(FONTS_DIR);
  scanFontsInDirectory(ROOT_FONTS_DIR);
  scanFontsInDirectory(HIDDEN_FONTS_DIR);

  LOG_DBG("FNT", "Total fonts found: %zu (including default)", fontFiles.size());

  // Find currently selected font index
  selectedIndex = 0;  // Default
  const bool hasCurrent = isSystemTarget() ? SETTINGS.hasSystemFont() : SETTINGS.hasCustomFont();
  const char* currentPath = isSystemTarget() ? SETTINGS.systemFontPath : SETTINGS.customFontPath;
  if (hasCurrent) {
    selectedIndex = indexOfFont(fontFiles, currentPath);
  }
}

void FontSelectionActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  displayMutex = xSemaphoreCreateMutex();

  // Load font list from SD card
  loadFontList();

  updateRequired = true;

  xTaskCreate(&FontSelectionActivity::taskTrampoline, "FontSelectionTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void FontSelectionActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(displayMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(displayMutex);
  displayMutex = nullptr;
}

void FontSelectionActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int itemCount = static_cast<int>(fontNames.size());
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex + itemCount - 1) % itemCount;
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % itemCount;
    updateRequired = true;
  }
}

void FontSelectionActivity::handleSelection() {
  xSemaphoreTake(displayMutex, portMAX_DELAY);

  // Show loading screen
  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 - 10, "글꼴 적용 중...");
  renderer.displayBuffer();

  // Update the target font path in settings
  char* destPath = isSystemTarget() ? SETTINGS.systemFontPath : SETTINGS.customFontPath;
  const size_t destSize = isSystemTarget() ? sizeof(SETTINGS.systemFontPath) : sizeof(SETTINGS.customFontPath);
  if (selectedIndex == 0) {
    // Default selected - clear the path
    destPath[0] = '\0';
  } else {
    strncpy(destPath, fontFiles[selectedIndex].c_str(), destSize - 1);
    destPath[destSize - 1] = '\0';
  }

  SETTINGS.saveToFile();
  LOG_DBG("FNT", "Font selected (%s): %s", isSystemTarget() ? "system" : "reader",
          selectedIndex == 0 ? "default" : destPath);

  if (isSystemTarget()) {
    // Reload UI system font dynamically (no reboot needed). UI-only, so reader caches
    // are unaffected and must NOT be invalidated.
    reloadSystemFont();
  } else {
    // Reload custom reader font dynamically (no reboot needed)
    reloadCustomReaderFont();
    // Invalidate EPUB/TXT caches since the reader font changed
    invalidateReaderCaches();
  }

  xSemaphoreGive(displayMutex);

  // Return to settings
  finish();
}

void FontSelectionActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(displayMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(displayMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void FontSelectionActivity::render() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Draw header
  renderer.drawCenteredText(UI_12_FONT_ID, 15, isSystemTarget() ? "시스템 글꼴 선택" : "글꼴 선택", true,
                            EpdFontFamily::BOLD);

  // Calculate visible items (with scrolling if needed)
  constexpr int lineHeight = 30;
  constexpr int startY = 60;
  const int maxVisibleItems = (pageHeight - startY - 50) / lineHeight;
  const int itemCount = static_cast<int>(fontNames.size());

  // Calculate scroll offset to keep selected item visible
  int scrollOffset = 0;
  if (itemCount > maxVisibleItems) {
    if (selectedIndex >= maxVisibleItems) {
      scrollOffset = selectedIndex - maxVisibleItems + 1;
    }
  }

  // Determine current selection (for checkmark comparison)
  int currentSelectedIndex = 0;  // Default
  const bool hasCurrent = isSystemTarget() ? SETTINGS.hasSystemFont() : SETTINGS.hasCustomFont();
  const char* currentPath = isSystemTarget() ? SETTINGS.systemFontPath : SETTINGS.customFontPath;
  if (hasCurrent) {
    currentSelectedIndex = indexOfFont(fontFiles, currentPath);
  }

  // Draw font list
  for (int i = 0; i < maxVisibleItems && (i + scrollOffset) < itemCount; i++) {
    const int itemIndex = i + scrollOffset;
    const int itemY = startY + i * lineHeight;
    const bool isHighlighted = (itemIndex == selectedIndex);
    const bool isCurrentFont = (itemIndex == currentSelectedIndex);

    // Draw selection highlight
    if (isHighlighted) {
      renderer.fillRect(0, itemY - 2, pageWidth - 1, lineHeight);
    }

    // Draw checkmark for currently active font (using asterisk - available in Pretendard)
    if (isCurrentFont) {
      renderer.drawText(UI_10_FONT_ID, 10, itemY, "*", !isHighlighted);
    }

    // Draw font name
    renderer.drawText(UI_10_FONT_ID, 35, itemY, fontNames[itemIndex].c_str(), !isHighlighted);
  }

  // Draw scroll indicators if needed
  if (scrollOffset > 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, startY - 15, "...", true);
  }
  if (scrollOffset + maxVisibleItems < itemCount) {
    renderer.drawCenteredText(UI_10_FONT_ID, startY + maxVisibleItems * lineHeight, "...", true);
  }

  // Draw help text
  const auto labels = mappedInput.mapLabels("« 뒤로", "선택", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
