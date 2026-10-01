#pragma once
#include <GfxRenderer.h>
#include <string>
#include <utility>
struct MappedInputManager {};
struct ScreenshotInfo {
  enum class ReaderType { Txt }; ReaderType readerType; char title[128]{};
  int currentPage=0,totalPages=0,progressPercent=0;
};
class ReaderActivity {
 protected:
  GfxRenderer& renderer;
  std::string bookPath;
  int pagesUntilFullRefresh=0;
  ReaderActivity(const char*,GfxRenderer& r,MappedInputManager&,std::string path,bool):renderer(r),bookPath(std::move(path)){}
  virtual bool loadBook()=0;
  virtual std::string getBookTitle() const=0;
  virtual void renderBook()=0;
 public:
  virtual ~ReaderActivity()=default;
  virtual bool pageTurn(bool)=0; virtual bool skipPages(int)=0; virtual bool isAtEndOfBook() const=0;
  virtual void onReturnFromEndOfBook()=0; virtual ScreenshotInfo getScreenshotInfo() const=0;
};
inline void vTaskDelay(int) {}
