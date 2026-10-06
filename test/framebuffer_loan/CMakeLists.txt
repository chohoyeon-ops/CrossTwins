production_slice(LOAN_SOURCE lib/GfxRenderer/GfxRenderer.cpp
  "void GfxRenderer::releaseFrameBufferForBuild()" "bool GfxRenderer::isFontCacheScanning()")
production_slice(DISPLAY_SOURCE freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp
  "uint8_t* FreeInkDisplay::lendBuildStorage" "#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE\n// Secondary-buffer release/realloc")
production_slice(LOAN_DECL lib/GfxRenderer/GfxRenderer.h
  "  class FrameBufferLoan {" "  // Low level functions")
configure_file(LoanTest.cpp.in LoanTest.cpp @ONLY)
add_executable(FrameBufferLoanTest "${CMAKE_CURRENT_BINARY_DIR}/LoanTest.cpp"
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp)
target_include_directories(FrameBufferLoanTest PRIVATE ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/inflate_stream/stubs)
target_link_libraries(FrameBufferLoanTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FrameBufferLoanTest)
