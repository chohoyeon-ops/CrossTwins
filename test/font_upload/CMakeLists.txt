production_slice(FONT_HANDLERS src/network/CrossPointWebServer.cpp
  "void CrossPointWebServer::handleFontUploadData()" "void CrossPointWebServer::handleFontDelete()")
production_slice(FONT_STATE src/network/CrossPointWebServer.h
  "  struct FontUploadState {" "  // OPDS server handlers")
production_slice(FONT_VALIDATE src/FontInstaller.cpp
  "bool FontInstaller::isValidFamilyName(" "bool FontInstaller::validateCpfontFile(")
production_slice(FONT_PATH src/FontInstaller.cpp
  "void FontInstaller::buildFontPath(" "FontInstaller::Error FontInstaller::deleteFamily(")
configure_file(FontUploadTest.cpp.in FontUploadTest.cpp @ONLY)
add_executable(FontUploadTest "${CMAKE_CURRENT_BINARY_DIR}/FontUploadTest.cpp")
target_link_libraries(FontUploadTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FontUploadTest)
