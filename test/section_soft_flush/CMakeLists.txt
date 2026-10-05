production_slice(SECTION_VERSIONS lib/Epub/Epub/Section.cpp
  "constexpr uint8_t SECTION_FILE_VERSION" "constexpr uint32_t HEADER_SIZE")
production_slice(SECTION_GATE lib/Epub/Epub/Section.cpp
  "bool Section::loadSectionFile(" "    int fileFontId;")
configure_file(SectionSoftFlushTest.cpp.in SectionSoftFlushTest.cpp @ONLY)
add_executable(SectionSoftFlushTest "${CMAKE_CURRENT_BINARY_DIR}/SectionSoftFlushTest.cpp")
target_include_directories(SectionSoftFlushTest PRIVATE
  "${REPO_ROOT}/test/chapter_html_slim_parser/stubs"
  "${REPO_ROOT}/lib/Serialization")
target_link_libraries(SectionSoftFlushTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SectionSoftFlushTest)
