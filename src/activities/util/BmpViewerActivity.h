#pragma once

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return slideshowActive; }

 private:
  void loadSiblingImages();
  void doSetSleepCover();
  bool canSetSleepCover() const;
  bool renderPng();
  bool renderCurrentImage();
  bool openSibling(int delta, bool wrap = false);
  void drawViewerHints();
  void drawSideHints();
  void showSlideshowError();
  void openIntervalPicker();

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
  bool slideshowActive = false;
  bool slideshowError = false;
  uint32_t lastSlideDisplayMs = 0;
};
