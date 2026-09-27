#include "BmpViewerActivity.h"

#include <Bitmap.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char CUSTOM_SLEEP_ROOT_BMP[] = "/sleep.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr size_t COPY_BUFFER_SIZE = 2048;
}  // namespace

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("BmpViewer", renderer, mappedInput), filePath(std::move(path)) {}

void BmpViewerActivity::loadSiblingImages() {
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.') {
        std::string fname(name);
        if (FsHelpers::hasBmpExtension(fname) || FsHelpers::hasPngExtension(fname)) {
          siblingImages.push_back(fname);
        }
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(siblingImages);

  const auto image = std::find(siblingImages.begin(), siblingImages.end(), fileName);
  if (image != siblingImages.end()) {
    currentImageIndex = static_cast<int>(image - siblingImages.begin());
  }
}

bool BmpViewerActivity::canSetSleepCover() const {
  return FsHelpers::hasBmpExtension(filePath) ||
         (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM &&
          FsHelpers::hasPngExtension(filePath));
}

bool BmpViewerActivity::renderPng() {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(filePath, dimensions)) return false;
  if (dimensions.width <= 0 || dimensions.height <= 0) return false;

  const float scale = std::min(static_cast<float>(renderer.getScreenWidth()) / dimensions.width,
                               static_cast<float>(renderer.getScreenHeight()) / dimensions.height);
  const int width = std::min(renderer.getScreenWidth(), static_cast<int>(dimensions.width * std::min(scale, 1.0f)));
  const int height = std::min(renderer.getScreenHeight(), static_cast<int>(dimensions.height * std::min(scale, 1.0f)));
  RenderConfig config{(renderer.getScreenWidth() - width) / 2, (renderer.getScreenHeight() - height) / 2, width,
                      height};

  PngToFramebufferConverter converter;
  return converter.decodeToFramebuffer(filePath, renderer, config);
}

void BmpViewerActivity::onEnter() {
  Activity::onEnter();

  if (siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  renderCurrentImage();
}

void BmpViewerActivity::render(RenderLock&&) {
  if (slideshowError) {
    showSlideshowError();
  } else {
    renderCurrentImage();
  }
}

void BmpViewerActivity::drawSideHints() {
  if (gpio.hasTouch()) return;
  const int width = UITheme::getInstance().getMetrics().sideButtonHintsWidth;
  const int height = 78;
  const int left = gpio.hasEdgeSideButtons() ? 4 : renderer.getScreenWidth() - width - 4;
  const int top = gpio.hasEdgeSideButtons() ? 155 : 345;

  auto drawHint = [this, width, height](int x, int y, bool previous) {
    const bool grayscalePlane = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();
    renderer.fillRect(x, y, width, height, grayscalePlane);
    if (grayscalePlane) return;
    renderer.drawRect(x, y, width, height);
    const int centerX = x + width / 2;
    const int centerY = y + height / 2;
    for (int offset = 0; offset < 7; ++offset) {
      const int row = previous ? centerY - 6 + offset : centerY + 6 - offset;
      renderer.drawLine(centerX - offset, row, centerX + offset, row);
    }
  };
  drawHint(left, top, true);
  drawHint(gpio.hasEdgeSideButtons() ? renderer.getScreenWidth() - width - 4 : left,
           gpio.hasEdgeSideButtons() ? top : top + height + 5, false);
}

void BmpViewerActivity::drawViewerHints() {
  if (slideshowActive) return;
  const auto labelForHardware = [this](uint8_t hardware) -> const char* {
    if (hardware == SETTINGS.frontButtonBack) return tr(STR_BACK);
    if (hardware == SETTINGS.frontButtonConfirm) return canSetSleepCover() ? tr(STR_SET_SLEEP_COVER) : "";
    if (hardware == SETTINGS.frontButtonLeft) return tr(STR_SLIDESHOW_PLAY);
    if (hardware == SETTINGS.frontButtonRight) return tr(STR_SETTINGS_TITLE);
    return "";
  };
  GUI.drawButtonHints(renderer, labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
                      labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT));
  drawSideHints();
}

bool BmpViewerActivity::renderCurrentImage() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  Rect popupRect{};
  if (!slideshowActive) {
    popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    GUI.fillPopupProgress(renderer, popupRect, 20);  // Initial 20% progress
  }
  if (FsHelpers::hasPngExtension(filePath)) {
    renderer.clearScreen();
    if (renderPng()) {
      drawViewerHints();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      return true;
    } else {
      if (slideshowActive) return false;
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      return false;
    }
  }

  HalFile file;
  // 1. Open the BMP file
  if (Storage.openFileForRead("BMP", filePath, file)) {
    Bitmap bitmap(file, true,
                  renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
                      display.getController() == HalDisplay::Controller::SSD1677);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x, y;

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          // Wider than screen
          x = 0;
          y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
        } else {
          // Taller than screen
          x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
          y = 0;
        }
      } else {
        // Center small images
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      // 4. Prepare Rendering
      if (!slideshowActive) GUI.fillPopupProgress(renderer, popupRect, 50);

      renderer.clearScreen();
      if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
        if (slideshowActive) return false;
        renderer.clearScreen();
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        return false;
      }

      // Draw UI hints on the base layer
      drawViewerHints();
      if (bitmap.hasGreyscale()) {
        const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
        if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return false;
        if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
        bool planesReady = true;
        for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
          if (bitmap.rewindToData() != BmpReaderError::Ok) {
            LOG_ERR("BMP", "Failed to rewind bitmap for grayscale rendering");
            planesReady = false;
            break;
          }
          renderer.clearScreen(absolute ? 0xFF : 0x00);
          renderer.setRenderMode(mode);
          if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
            planesReady = false;
            break;
          }
          drawViewerHints();
          if (mode == GfxRenderer::GRAYSCALE_LSB) {
            renderer.copyGrayscaleLsbBuffers();
          } else {
            renderer.copyGrayscaleMsbBuffers();
          }
        }
        if (planesReady) renderer.displayGrayBuffer();

        // Rebuild the BW framebuffer for popups and subsequent differential updates.
        renderer.setRenderMode(GfxRenderer::BW);
        renderer.clearScreen();
        if (bitmap.rewindToData() != BmpReaderError::Ok ||
            !renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
          LOG_ERR("BMP", "Failed to rewind bitmap to restore the BW framebuffer");
          renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
          planesReady = false;
        }
        drawViewerHints();
        renderer.cleanupGrayscaleWithFrameBuffer();
        if (!planesReady && !slideshowActive) renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        if (!planesReady) return false;
      } else {
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      }

      return true;

    } else {
      // Handle file parsing error
      if (slideshowActive) return false;
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_BMP_FILE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      return false;
    }

  } else {
    // Handle file open error
    if (slideshowActive) return false;
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return false;
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void BmpViewerActivity::doSetSleepCover() {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  const bool transparentMode = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM;
  if (!canSetSleepCover()) return;

  const char* destination =
      transparentMode ? (FsHelpers::hasPngExtension(filePath) ? TRANSPARENT_SLEEP_ROOT_PNG : TRANSPARENT_SLEEP_ROOT_BMP)
                      : CUSTOM_SLEEP_ROOT_BMP;
  bool success = filePath == destination;

  if (!success) {
    auto buffer = makeUniqueNoThrow<uint8_t[]>(COPY_BUFFER_SIZE);
    if (!buffer) {
      LOG_ERR("BMP", "OOM: sleep cover copy buffer");
    } else {
      HalFile inFile, outFile;
      if (Storage.openFileForRead("BMP", filePath, inFile) && Storage.openFileForWrite("BMP", destination, outFile)) {
        int bytesRead;
        success = true;
        while ((bytesRead = inFile.read(buffer.get(), COPY_BUFFER_SIZE)) > 0) {
          if (outFile.write(buffer.get(), static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
            success = false;
            break;
          }
        }
        if (bytesRead < 0) success = false;
        outFile.close();
      }
    }
  }

  if (success) {
    if (!transparentMode) SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    GUI.drawPopup(renderer, tr(STR_DONE));
  } else {
    GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
  }

  delay(1000);
  onEnter();
}

bool BmpViewerActivity::openSibling(const int delta, const bool wrap) {
  if (currentImageIndex < 0 || siblingImages.size() <= 1) return false;
  int nextIndex = currentImageIndex + delta;
  if (wrap && nextIndex == static_cast<int>(siblingImages.size())) nextIndex = 0;
  if (nextIndex < 0 || nextIndex >= static_cast<int>(siblingImages.size())) return false;

  currentImageIndex = nextIndex;
  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  if (dirPath.back() != '/') dirPath += "/";
  filePath = dirPath + siblingImages[currentImageIndex];
  return renderCurrentImage();
}

void BmpViewerActivity::showSlideshowError() {
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  const int height = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_10_FONT_ID, height / 2 - renderer.getLineHeight(UI_10_FONT_ID), tr(STR_SLIDESHOW_ERROR));
  const char* filename = strrchr(filePath.c_str(), '/');
  filename = filename ? filename + 1 : filePath.c_str();
  const auto lines = renderer.wrappedText(UI_10_FONT_ID, filename, renderer.getScreenWidth() - 40, 2);
  for (size_t i = 0; i < lines.size(); ++i) {
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2 + static_cast<int>(i) * renderer.getLineHeight(UI_10_FONT_ID),
                              lines[i].c_str());
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void BmpViewerActivity::openIntervalPicker() {
  auto activity = makeUniqueNoThrow<IntervalSelectionActivity>(
      renderer, mappedInput, "SlideshowInterval", StrId::STR_SLIDESHOW_INTERVAL, SETTINGS.slideshowIntervalSeconds,
      CrossPointSettings::MIN_SLIDESHOW_INTERVAL_SECONDS, CrossPointSettings::MAX_SLIDESHOW_INTERVAL_SECONDS, 10, 60,
      StrId::STR_NONE_OPT, false, StrId::STR_NONE_OPT, true);
  if (!activity) {
    LOG_ERR("BMP", "OOM: slideshow interval picker");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      const auto selected = static_cast<uint16_t>(std::get<IntervalResult>(result.data).value);
      if (selected != SETTINGS.slideshowIntervalSeconds) {
        SETTINGS.slideshowIntervalSeconds = selected;
        SETTINGS.saveToFile();
      }
    }
    requestUpdate();
  });
}

void BmpViewerActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (slideshowActive) {
      slideshowActive = false;
      renderCurrentImage();
      return;
    }
    activityManager.goToFileBrowser(filePath);
    return;
  }

  if (slideshowActive) {
    if (siblingImages.size() > 1 && static_cast<uint32_t>(millis() - lastSlideDisplayMs) >=
                                        static_cast<uint32_t>(SETTINGS.slideshowIntervalSeconds) * 1000U) {
      if (openSibling(1, true)) {
        lastSlideDisplayMs = millis();
      } else {
        slideshowActive = false;
        slideshowError = true;
        showSlideshowError();
      }
    }
    return;
  }
  if (slideshowError) return;

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    openSibling(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (canSetSleepCover()) doSetSleepCover();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    slideshowActive = true;
    if (renderCurrentImage()) {
      lastSlideDisplayMs = millis();
    } else {
      slideshowActive = false;
      slideshowError = true;
      showSlideshowError();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    openIntervalPicker();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    openSibling(1);
    return;
  }
}
