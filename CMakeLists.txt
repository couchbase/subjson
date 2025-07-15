project(subjson LANGUAGES C CXX)

include(CTest)

if (WIN32)
    # Set Compiler warning level to /W4
    add_compile_options(/W4 /wd4505)
    #if (COUCHBASE_KV_COMMIT_VALIDATION)
        # Enable warnings as errors when building under CV
        #add_compile_definitions(/WX)
    #endif ()
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
