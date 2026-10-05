# Exercise the actual production callback bodies with synchronous input adapters.
file(READ "${REPO_ROOT}/src/util/ButtonNavigator.h" NAV_HEADER)
string(REPLACE "#pragma once" "" NAV_HEADER "${NAV_HEADER}")
string(REPLACE "#include \"MappedInputManager.h\"" "" NAV_HEADER "${NAV_HEADER}")
file(READ "${REPO_ROOT}/src/util/ButtonNavigator.cpp" NAV_SOURCE)
string(REPLACE "#include \"ButtonNavigator.h\"" "" NAV_SOURCE "${NAV_SOURCE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${REPO_ROOT}/src/util/ButtonNavigator.h" "${REPO_ROOT}/src/util/ButtonNavigator.cpp")
production_slice(LIST_NAV src/activities/UiListActivity.cpp
  "void UiListActivity::navigateButtons()" "void UiListActivity::syncListViewport")
production_slice(LIBRARY_NAV src/activities/library/LibraryListActivity.cpp
  "void LibraryListActivity::navigateButtons()" "void LibraryListActivity::buildRows")
production_slice(HOME_NAV src/activities/home/HomeActivity.cpp
  "    const int bookCount = static_cast<int>(recentBooks.size());"
  "    return;\n  }\n\n  const int coverColumnCount")
configure_file(Phase2bTest.cpp.in Phase2bTest.cpp @ONLY)
add_executable(Phase2bTest "${CMAKE_CURRENT_BINARY_DIR}/Phase2bTest.cpp")
target_link_libraries(Phase2bTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(Phase2bTest)
