cb_add_test_executable(subjson-test t_match.cc t_ops.cc t_path.cc t_uescape.cc t_validate.cc)
target_link_libraries(subjson-test subjson GTest::gtest GTest::gtest_main)
add_test(NAME subjson-all-tests COMMAND subjson-test)
