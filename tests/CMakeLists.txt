cb_add_test_executable(subjson-test t_invariants.cc t_match.cc t_ops.cc t_path.cc t_uescape.cc t_validate.cc)
target_link_libraries(subjson-test subjson JSON_checker GTest::gtest GTest::gtest_main)
cb_enable_unity_build(subjson-test)
add_sanitizers(subjson-test)
add_test(NAME subjson-all-tests COMMAND subjson-test)

cb_add_test_executable(subjson-bench subjson_bench.cc)
target_link_libraries(subjson-bench subjson benchmark::benchmark)
