cmake_minimum_required(VERSION 3.10)

project(XrdPurgeLotMan VERSION 1.0 DESCRIPTION "XrdPurgeLotMan shared library")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option( XROOTD_PLUGINS_BUILD_UNITTESTS "Build the xrootd-lotman unit tests" OFF )
option( XROOTD_PLUGINS_EXTERNAL_GTEST "Use an external/pre-installed copy of GTest" OFF )

# Set the module path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

find_package(Xrootd REQUIRED)
find_package(Lotman REQUIRED)

# Include directories
include_directories(${XROOTD_INCLUDES})
include_directories(${LOTMAN_INCLUDES})

add_library(XrdPurgeLotMan SHARED
    src/XrdPurgeLotMan.cc
)

target_link_libraries(XrdPurgeLotMan
    PRIVATE ${LOTMAN_LIB}
    PRIVATE ${XROOTD_PFC_LIB}
    PRIVATE ${XROOTD_UTILS_LIB}
)

set_target_properties(XrdPurgeLotMan PROPERTIES VERSION ${PROJECT_VERSION})

set(LIB_INSTALL_DIR ${LIB_INSTALL_DIR} CACHE PATH "Library installation directory")
set(INCLUDE_INSTALL_DIR ${INCLUDE_INSTALL_DIR} CACHE PATH "Include installation directory")

install(TARGETS XrdPurgeLotMan
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
)

install(DIRECTORY src/
    DESTINATION ${INCLUDE_INSTALL_DIR}
    FILES_MATCHING PATTERN "*.hh"
)

if( XROOTD_PLUGINS_BUILD_UNITTESTS )
  if( NOT XROOTD_PLUGINS_EXTERNAL_GTEST )
    include(ExternalProject)
    ExternalProject_Add(gtest
      PREFIX external/gtest
      URL ${CMAKE_CURRENT_SOURCE_DIR}/vendor/gtest
      BUILD_BYPRODUCTS external/gtest/src/gtest-build/lib/libgtest.a
      INSTALL_COMMAND :
    )
  endif()
  enable_testing()
  add_subdirectory(test)
endif()
