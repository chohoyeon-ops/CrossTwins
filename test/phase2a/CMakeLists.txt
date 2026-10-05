# Compile unchanged production routines against small host adapters. This avoids
# pulling the display, activities and SD persistence into algorithm-only tests.
function(production_slice output path start finish)
  file(READ "${REPO_ROOT}/${path}" source)
  string(FIND "${source}" "${start}" first)
  string(FIND "${source}" "${finish}" last)
  if(first LESS 0 OR last LESS_EQUAL first)
    message(FATAL_ERROR "Cannot locate production test slice: ${path}")
  endif()
  math(EXPR length "${last} - ${first}")
  string(SUBSTRING "${source}" ${first} ${length} result)
  set(${output} "${result}" PARENT_SCOPE)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPO_ROOT}/${path}")
endfunction()

file(READ "${REPO_ROOT}/src/util/ButtonNavigator.h" NAV_HEADER)
string(REPLACE "#pragma once" "" NAV_HEADER "${NAV_HEADER}")
string(REPLACE "#include \"MappedInputManager.h\"" "" NAV_HEADER "${NAV_HEADER}")
file(READ "${REPO_ROOT}/src/util/ButtonNavigator.cpp" NAV_SOURCE)
string(REPLACE "#include \"ButtonNavigator.h\"" "" NAV_SOURCE "${NAV_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${REPO_ROOT}/src/util/ButtonNavigator.h"
  "${REPO_ROOT}/src/util/ButtonNavigator.cpp")
file(READ "${REPO_ROOT}/src/RecentBooksStore.h" recent_header)
string(REGEX MATCH "static constexpr int MAX_RECENT_BOOKS = [0-9]+;" RECENT_LIMIT "${recent_header}")
if(NOT RECENT_LIMIT)
  message(FATAL_ERROR "Cannot locate current Recent Books capacity")
endif()
production_slice(RECENT_RECORD src/RecentBooksStore.h "struct RecentBook" "class RecentBooksStore")
production_slice(RECENT_ADD src/RecentBooksStore.cpp "void RecentBooksStore::addBook" "void RecentBooksStore::updateBook")
production_slice(RECENT_PRUNE src/RecentBooksStore.cpp "bool RecentBooksStore::isMissing" "RecentBook RecentBooksStore::getDataFromBook")
production_slice(TRUNCATE lib/GfxRenderer/GfxRenderer.cpp "std::string GfxRenderer::truncatedText" "std::vector<std::string> GfxRenderer::wrappedText")
production_slice(SCREENSHOT src/util/ScreenshotUtil.cpp "void ScreenshotUtil::buildFilename" "void ScreenshotUtil::takeScreenshot")
configure_file(Phase2aTest.cpp.in Phase2aTest.cpp @ONLY)

add_executable(Phase2aTest
  "${CMAKE_CURRENT_BINARY_DIR}/Phase2aTest.cpp"
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
)
target_include_directories(Phase2aTest PRIVATE
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/EpdFont
)
target_link_libraries(Phase2aTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(Phase2aTest)
