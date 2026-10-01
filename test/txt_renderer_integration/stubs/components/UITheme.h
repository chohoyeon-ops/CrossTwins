#pragma once
#include <GfxRenderer.h>
#include <string>
class UITheme {
 public:
  static UITheme& getInstance() { static UITheme theme; return theme; }
  int getStatusBarHeight() const { return 0; }
  void drawPopup(GfxRenderer&,const char*) {}
  void drawStatusBar(GfxRenderer&,float,int,int,const std::string&) {}
};
#define GUI UITheme::getInstance()
