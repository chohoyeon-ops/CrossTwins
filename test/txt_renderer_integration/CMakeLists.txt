add_executable(TxtRendererIntegrationTest
  TxtRendererIntegrationTest.cpp
  TxtReaderIntegrationTest.cpp
  HostBitmap.cpp
  ${REPO_ROOT}/src/activities/reader/TxtReaderActivity.cpp
  ${REPO_ROOT}/src/util/BookProgress.cpp
  ${REPO_ROOT}/lib/Txt/Txt.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
)
target_include_directories(TxtRendererIntegrationTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Txt
)
target_compile_definitions(TxtRendererIntegrationTest PRIVATE CROSSPOINT_RENDERER_TEST_TRACE=1 CROSSPOINT_TXT_READER_TEST=1)
target_compile_options(TxtRendererIntegrationTest PRIVATE -fno-exceptions)
target_link_libraries(TxtRendererIntegrationTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(TxtRendererIntegrationTest)
