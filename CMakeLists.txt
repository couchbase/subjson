cmake_minimum_required(VERSION 3.15)
project(subjson LANGUAGES C CXX)

include(CTest)
include(check_unit_test_enabled)

check_unit_test_enabled(subjson COUCHBASE_SUBJSON_BUILD_UNIT_TESTS)

enable_code_coverage_report()

if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_library(subjson STATIC
            subdoc/match.cc
            subdoc/operations.cc
            subdoc/path.cc
            subdoc/util.cc)
target_include_directories(subjson PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(subjson PUBLIC platform)

if (COUCHBASE_SUBJSON_BUILD_UNIT_TESTS)
    add_executable(bench bench.cc contrib/cliopts/cliopts.c)
    target_link_libraries(bench subjson)
    enable_testing()
    add_subdirectory(tests)
endif (COUCHBASE_SUBJSON_BUILD_UNIT_TESTS)
