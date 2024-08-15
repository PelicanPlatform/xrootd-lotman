find_library(LOTMAN_LIB LotMan
    NAMES LotMan
    PATHS /usr/local/lib64 /usr/lib64
)

if(NOT LOTMAN_LIB)
    message(FATAL_ERROR "libLotMan.so not found")
endif()

find_path(LOTMAN_INCLUDES
    NAMES lotman.h
    PATHS /usr /usr/local
    PATH_SUFFIXES include/lotman   
)

if(NOT LOTMAN_INCLUDES)
    message(FATAL_ERROR "header file 'lotman.h' not found")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Lotman DEFAULT_MSG LOTMAN_INCLUDES LOTMAN_LIB)
