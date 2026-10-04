add_executable(TxtOnDemandTest
  TxtOnDemandTest.cpp
  ${REPO_ROOT}/lib/Txt/TxtPageEstimate.cpp
  ${REPO_ROOT}/lib/Txt/TxtPageReader.cpp
  ${REPO_ROOT}/lib/Txt/TxtProgressFile.cpp
)

target_include_directories(TxtOnDemandTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Txt
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(TxtOnDemandTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TxtOnDemandTest)
