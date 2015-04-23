IF(WIN32)
    SET(gtest_force_shared_crt ON CACHE BOOL "")
    SET(BUILD_SHARED_LIBS ON)
ENDIF()

# Only add the embedded gtest submodule if it isn't already present.
IF(NOT EXISTS ${gtest_SOURCE_DIR})
    ADD_SUBDIRECTORY(${SUBJSON_GTEST} subjson_gtest)
ENDIF(NOT EXISTS ${gtest_SOURCE_DIR})

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/src)
INCLUDE_DIRECTORIES(${gtest_SOURCE_DIR} ${gtest_SOURCE_DIR}/include)

FILE(GLOB T_SUBJS_SRC t_*.cc)
ADD_EXECUTABLE(subjson-test ${T_SUBJS_SRC} subdoc-tests-common.cc)

TARGET_LINK_LIBRARIES(subjson-test subjson gtest gtest_main)
ADD_TEST(NAME subjson-all-tests COMMAND $<TARGET_FILE:subjson-test>)
