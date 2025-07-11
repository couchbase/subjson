project(subjson LANGUAGES C CXX)

include(CTest)

enable_code_coverage_report()

add_library(subjson STATIC
            subdoc/match.cc
            subdoc/operations.cc
            subdoc/path.cc
            subdoc/util.cc)
target_include_directories(subjson PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(subjson PUBLIC platform)

enable_testing()
add_subdirectory(tests)
