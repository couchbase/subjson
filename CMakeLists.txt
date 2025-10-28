project(subjson LANGUAGES C CXX)

include(CTest)

if (WIN32)
    # Disable C4505 - "unreferenced local function has been removed,"
    add_compile_options(/wd4505)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
            -Werror=shorten-64-to-32
            -Werror=implicit-int-conversion)
endif ()

enable_code_coverage_report()

add_library(subjson STATIC
            subdoc/match.cc
            subdoc/operations.cc
            subdoc/path.cc
            subdoc/util.cc)
target_include_directories(subjson PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(subjson PUBLIC platform gsl::gsl-lite)
add_sanitizers(subjson)

enable_testing()
add_subdirectory(tests)
