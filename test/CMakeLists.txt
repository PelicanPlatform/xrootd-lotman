add_executable( xrootd-lotman-gtest xrootd-lotman-tests.cc
  ../src/XrdPurgeLotMan.cc
)

# Define the absolute path to the resources directory for tests
target_compile_definitions(xrootd-lotman-gtest PRIVATE
    TEST_RESOURCES_DIR="${CMAKE_CURRENT_BINARY_DIR}/resources"
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

# Copy the resources directory to the build/test directory
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            ${CMAKE_CURRENT_BINARY_DIR}/resources
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources
    COMMENT "Copying test resources directory"
)

add_custom_target(copy_resources ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/resources
)

add_dependencies(xrootd-lotman-gtest copy_resources)
