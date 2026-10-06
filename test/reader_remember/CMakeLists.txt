production_slice(REMEMBER_STATE src/activities/reader/ReaderActivity.h
  "  std::atomic<bool> pageRendered" "  explicit ReaderActivity")
production_slice(ENTER src/activities/reader/ReaderActivity.cpp
  "void ReaderActivity::onEnter()" "void ReaderActivity::onExit()")
production_slice(EXIT src/activities/reader/ReaderActivity.cpp
  "void ReaderActivity::onExit()" "bool ReaderActivity::handleBackNavigation()")
production_slice(TXT_RENDER src/activities/reader/TxtReaderActivity.cpp
  "void TxtReaderActivity::renderBook()" "void TxtReaderActivity::updateLayoutIfChanged")
production_slice(XTC_RENDER src/activities/reader/XtcReaderActivity.cpp
  "void XtcReaderActivity::renderPage()" "bool XtcReaderActivity::pageTurn")
production_slice(EPUB_SUCCESS src/activities/reader/EpubReaderActivity.cpp
  "    lastRenderCompleteMs = millis();" "  }\n\n  if (currentSpineIndex != lastSavedSpineIndex")
production_slice(EPUB_END src/activities/reader/EpubReaderActivity.cpp
  "void EpubReaderActivity::onEndOfBookRendered()" "bool EpubReaderActivity::applyDeferredReposition()")
production_slice(EPUB_PROGRESS src/activities/reader/EpubReaderActivity.cpp
  "  HalFile f;\n  if (Storage.openFileForRead(\"ERS\"" "  if (currentSpineIndex == 0)")
production_slice(XTC_PROGRESS src/activities/reader/XtcReaderActivity.cpp
  "void XtcReaderActivity::loadProgress()" "ScreenshotInfo XtcReaderActivity::getScreenshotInfo()")
configure_file(RememberTest.cpp.in RememberTest.cpp @ONLY)
add_executable(ReaderRememberTest "${CMAKE_CURRENT_BINARY_DIR}/RememberTest.cpp")
target_link_libraries(ReaderRememberTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderRememberTest)
