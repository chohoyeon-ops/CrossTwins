file(READ "${REPO_ROOT}/src/activities/RenderLock.h" LOCK_HEADER)
string(REPLACE "#pragma once" "" LOCK_HEADER "${LOCK_HEADER}")
production_slice(LOCK_SOURCE src/activities/ActivityManager.cpp "RenderLock::RenderLock(Mode" "/**")
production_slice(BUILD_HINT src/activities/reader/EpubReaderActivity.cpp
  "bool EpubReaderActivity::backgroundBuildWanted()" "void EpubReaderActivity::renderBook()")
production_slice(BUILD_TICK src/activities/reader/EpubReaderActivity.cpp
  "  {\n    RenderLock lock(RenderLock::Mode::Try);\n    // Not under an open toolbar"
  "  const bool atEndOfBook")
production_slice(MAIN_HINT src/main.cpp "  bool skipLoopDelay = false;" "  // Add delay at the end of the loop")
configure_file(ContentionTest.cpp.in ContentionTest.cpp @ONLY)
add_executable(ReaderContentionTest "${CMAKE_CURRENT_BINARY_DIR}/ContentionTest.cpp")
target_link_libraries(ReaderContentionTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderContentionTest)
