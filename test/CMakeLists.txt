add_executable( xrootd-lotman-gtest xrootd-lotman-tests.cc
  ../src/XrdPurgeLotMan.cc
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

if(XROOTD_PLUGINS_EXTERNAL_GTEST)
    set(LIBGTEST "gtest")
else()
    add_dependencies(xrootd-lotman-gtest gtest)
    include_directories("${PROJECT_SOURCE_DIR}/vendor/gtest/googletest/include")
    set(LIBGTEST "${CMAKE_BINARY_DIR}/external/gtest/src/gtest-build/lib/libgtest.a")
endif()

target_link_libraries(xrootd-lotman-gtest XrdPurgeLotMan
    ${LIBGTEST}
    ${LOTMAN_LIB}
    ${XROOTD_PFC_LIB}
    ${XROOTD_UTILS_LIB}
)

add_test(
  NAME
    xrootd-lotman-unit
  COMMAND
    ${CMAKE_CURRENT_BINARY_DIR}/xrootd-lotman-gtest
)
