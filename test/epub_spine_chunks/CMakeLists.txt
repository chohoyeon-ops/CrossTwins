production_slice(CHUNKS lib/Epub/Epub/BookMetadataCache.cpp
  "  if (spineCount >= LARGE_SPINE_THRESHOLD) {\n    LOG_DBG(\"BMC\", \"Using batch size lookup" "  uint32_t cumSize = 0;")
production_slice(TARGET lib/ZipFile/ZipFile.h "  struct SizeTarget" " private:")
production_slice(FILL lib/ZipFile/ZipFile.cpp "int ZipFile::fillUncompressedSizes" "uint8_t* ZipFile::readFileToMemory")
# Instrument entry into the unchanged lookup body, not its matching algorithm.
string(REPLACE "ZipFile::fillUncompressedSizes" "ZipFile::fillImpl" FILL "${FILL}")
production_slice(NORMALISE lib/FsHelpers/FsHelpers.cpp "std::string normalisePath" "namespace {\nint compareNatural")
configure_file(SpineChunksTest.cpp.in SpineChunksTest.cpp @ONLY)
add_executable(EpubSpineChunksTest "${CMAKE_CURRENT_BINARY_DIR}/SpineChunksTest.cpp")
target_link_libraries(EpubSpineChunksTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(EpubSpineChunksTest)
