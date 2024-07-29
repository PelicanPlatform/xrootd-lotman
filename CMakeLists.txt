cmake_minimum_required(VERSION 3.10)

project(XrdPurgeLotMan VERSION 1.0 DESCRIPTION "XrdPurgeLotMan shared library")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(XrdPurgeLotMan SHARED
    src/XrdPurgeLotMan.cc
)

include_directories(/usr/local/include/xrootd)
include_directories(/usr/lib64/include/xrootd)

# # Assuming lotman.h is in /usr/local/include, if not adjust accordingly
# target_include_directories(XrdPurgeLotMan PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}/src
#     /usr/local/include
# )

# # Link against libLotMan.so
# find_library(LOTMAN_LIBRARY NAMES LotMan PATHS /usr/local/lib64)
# if(NOT LOTMAN_LIBRARY)
#     message(FATAL_ERROR "libLotMan.so not found")
# endif()

# target_link_libraries(XrdPurgeLotMan
#     PRIVATE ${LOTMAN_LIBRARY}
#     # Add other libraries you need to link against
# )

# Find libLotMan.so
find_library(LOTMAN_LIBRARY NAMES LotMan PATHS /usr/local/lib64)
if(NOT LOTMAN_LIBRARY)
    message(FATAL_ERROR "libLotMan.so not found")
endif()

# Find the XrdPfc library
find_library(XRDPFC_LIBRARY NAMES XrdPfc-5 PATHS /usr/local/lib64 /usr/lib64)
if(NOT XRDPFC_LIBRARY)
    message(FATAL_ERROR "libXrdPfc-5.so not found")
endif()

# Link against both libLotMan.so and libXrdPfc-5.so
target_link_libraries(XrdPurgeLotMan
    PRIVATE ${LOTMAN_LIBRARY}
    PRIVATE ${XRDPFC_LIBRARY}
    # Add other libraries you need to link against
)

set_target_properties(XrdPurgeLotMan PROPERTIES VERSION ${PROJECT_VERSION})

install(TARGETS XrdPurgeLotMan
    LIBRARY DESTINATION lib
)