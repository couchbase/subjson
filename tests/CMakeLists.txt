IF(WIN32)
    SET(gtest_force_shared_crt ON CACHE BOOL "")
    SET(BUILD_SHARED_LIBS ON)
ENDIF()

# Only add the embedded gtest submodule if it isn't already present.
IF(NOT COUCHBASE_SERVER_BUILD AND EXISTS ${SUBJSON_GTEST})
    ADD_SUBDIRECTORY(${SUBJSON_GTEST} gtest)
ENDIF()

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/src)
INCLUDE_DIRECTORIES(${gtest_SOURCE_DIR} ${gtest_SOURCE_DIR}/include)

SET(T_SUBJS_SRC t_match.cc t_ops.cc t_path.cc t_uescape.cc t_validate.cc)
ADD_EXECUTABLE(subjson-test ${T_SUBJS_SRC})

TARGET_LINK_LIBRARIES(subjson-test subjson gtest gtest_main)
ADD_TEST(NAME subjson-all-tests COMMAND subjson-test)
