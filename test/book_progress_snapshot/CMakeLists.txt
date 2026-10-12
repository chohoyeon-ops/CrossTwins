add_executable(BookProgressSnapshotTest
  BookProgressSnapshotTest.cpp
  ${REPO_ROOT}/src/util/BookProgress.cpp
)
target_include_directories(BookProgressSnapshotTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
)
target_link_libraries(BookProgressSnapshotTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookProgressSnapshotTest)
