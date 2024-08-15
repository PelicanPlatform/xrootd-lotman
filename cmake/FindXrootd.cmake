find_path(XROOTD_INCLUDES XrdVersion.hh
    HINTS
    ${XROOTD_DIR}
    $ENV{XROOTD_DIR}
    /usr
    /usr/local
    /opt/xrootd/
    PATH_SUFFIXES include/xrootd
    PATHS /opt/xrootd
)

find_library(XROOTD_UTILS_LIB XrdUtils
    HINTS
    ${XROOTD_DIR}
    $ENV{XROOTD_DIR}
    /usr
    /usr/local
    /opt/xrootd/
    PATH_SUFFIXES lib
)

# XrdPfc.so is actually XrdPfc-5.so, where the version (presumably) comes from the
# XRootD version header file. The following chunk of code tries to predict the name of the lib.
# Not sure if this is the way I _should_ be doing this, but it seems to work...
file(READ "${XROOTD_INCLUDES}/XrdVersion.hh" XRD_VERSION_CONTENTS)
string(REGEX MATCH "#define XRDPLUGIN_SOVERSION \"([0-9]+)\"" _ ${XRD_VERSION_CONTENTS})
set(XRDPLUGIN_SOVERSION ${CMAKE_MATCH_1})
set(XROOTD_PFC_LIB_NAME "XrdPfc-${XRDPLUGIN_SOVERSION}")

find_library(XROOTD_PFC_LIB
    NAMES ${XROOTD_PFC_LIB_NAME} XrdPfc
    HINTS
    ${XROOTD_DIR}
    $ENV{XROOTD_DIR}
    /usr
    /usr/local
    /opt/xrootd/
    
    PATHS /usr/local/lib64 /usr/lib64
)
if( NOT XROOTD_PFC_LIB)
    message(FATAL_ERROR "libXrdPfc.so not found")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Xrootd DEFAULT_MSG XROOTD_UTILS_LIB XROOTD_PFC_LIB )
