add_executable(TxtPageLayoutTest
  TxtPageLayoutTest.cpp
)

target_include_directories(TxtPageLayoutTest PRIVATE
  ${REPO_ROOT}/lib/Txt
)

target_link_libraries(TxtPageLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TxtPageLayoutTest)
