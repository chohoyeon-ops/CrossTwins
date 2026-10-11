production_slice(FOCUS_LOAD src/CrossPointSettings.cpp
  "  // Retain compatibility with settings files written before Focus Reading was" "  const auto indentSpaces =")
production_slice(FOCUS_SAVE src/CrossPointSettings.cpp
  "  // Keep the legacy key stable for older settings consumers" "  // Front button remap")
configure_file(FocusReadingTest.cpp.in FocusReadingTest.cpp @ONLY)

add_executable(FocusReadingTest "${CMAKE_CURRENT_BINARY_DIR}/FocusReadingTest.cpp")
target_link_libraries(FocusReadingTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FocusReadingTest)
