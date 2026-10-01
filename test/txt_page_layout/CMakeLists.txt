add_executable(TxtPageLayoutTest
  TxtPageLayoutTest.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(TxtPageLayoutTest PRIVATE
  ${REPO_ROOT}/test/minibidi_arabic/stubs
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Txt
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(TxtPageLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TxtPageLayoutTest)
