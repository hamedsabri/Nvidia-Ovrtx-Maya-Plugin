# MIT License
# 
# Copyright (c) 2026 Hamed Sabri
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# FindMayaUSD.cmake
#
# This module defines imported targets:
#   MayaUsd::mayaUsd
#   MayaUsd::mayaUsdAPI
#
# Once you call:
#   find_package(MayaUsd REQUIRED)
#
# You do NOT need to manually add include directories or link directories.
# Simply link your target with:
#
#   target_link_libraries(yourTargetName
#       PRIVATE
#           MayaUsd::mayaUsd
#           MayaUsd::mayaUsdAPI
#   )
#
# This will automatically propagate the correct include paths and libraries.

include(FindPackageHandleStandardArgs)

set(_MAYAUSD_HINTS
    $ENV{MAYAUSD_INSTALL_LOCATION}
    ${MAYAUSD_INSTALL_LOCATION}
)

find_path(MAYAUSD_INCLUDE_DIR
    NAMES mayaUsd/mayaUsd.h
    PATH_SUFFIXES include
    HINTS ${_MAYAUSD_HINTS}
)

find_library(MAYAUSD_LIBRARY
    NAMES MayaUsd mayaUsd
    HINTS ${_MAYAUSD_HINTS}
    DOC "mayaUsd library"
    PATH_SUFFIXES lib
)

find_library(MAYAUSDAPI_LIBRARY
    NAMES MayaUsdAPI mayaUsdAPI
    PATH_SUFFIXES lib
    DOC "mayaUsdAPI library"
    HINTS ${_MAYAUSD_HINTS}
)

# Version detection from header macros
set(MayaUsd_VERSION "UNKNOWN")

if(MAYAUSD_INCLUDE_DIR)
    set(_header "${MAYAUSD_INCLUDE_DIR}/mayaUsd/mayaUsd.h")

    if(EXISTS "${_header}")
        file(STRINGS "${_header}" _major_str REGEX "^#define[ \t]+MAYAUSD_MAJOR_VERSION[ \t]+[0-9]+")
        file(STRINGS "${_header}" _minor_str REGEX "^#define[ \t]+MAYAUSD_MINOR_VERSION[ \t]+[0-9]+")
        file(STRINGS "${_header}" _patch_str REGEX "^#define[ \t]+MAYAUSD_PATCH_LEVEL[ \t]+[0-9]+")

        string(REGEX REPLACE ".*MAYAUSD_MAJOR_VERSION[ \t]+([0-9]+).*" "\\1" MAYAUSD_MAJOR "${_major_str}")
        string(REGEX REPLACE ".*MAYAUSD_MINOR_VERSION[ \t]+([0-9]+).*" "\\1" MAYAUSD_MINOR "${_minor_str}")
        string(REGEX REPLACE ".*MAYAUSD_PATCH_LEVEL[ \t]+([0-9]+).*" "\\1" MAYAUSD_PATCH "${_patch_str}")

        if(MAYAUSD_MAJOR MATCHES "[0-9]+" AND MAYAUSD_MINOR MATCHES "[0-9]+" AND MAYAUSD_PATCH MATCHES "[0-9]+")
            set(MayaUsd_VERSION "${MAYAUSD_MAJOR}.${MAYAUSD_MINOR}.${MAYAUSD_PATCH}")
        else()
            message(WARNING "Failed to parse MayaUsd version macros from ${_header}")
        endif()
    else()
        message(WARNING "mayaUsd.h not found at expected location: ${_header}")
    endif()
endif()

find_package_handle_standard_args(
    MayaUsd
    REQUIRED_VARS
        MAYAUSD_INCLUDE_DIR
        MAYAUSD_LIBRARY
        MAYAUSDAPI_LIBRARY
    VERSION_VAR MayaUsd_VERSION
)

if (NOT MayaUsd_FOUND)
    message(FATAL_ERROR "MayaUsd is not found!")
    return()
endif()

# Imported Target: MayaUsd::mayaUsd
add_library(MayaUsd::mayaUsd UNKNOWN IMPORTED)
set_target_properties(MayaUsd::mayaUsd PROPERTIES
    IMPORTED_LOCATION "${MAYAUSD_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MAYAUSD_INCLUDE_DIR}"
)

# Imported Target: MayaUsd::mayaUsdAPI
add_library(MayaUsd::mayaUsdAPI UNKNOWN IMPORTED)
set_target_properties(MayaUsd::mayaUsdAPI PROPERTIES
    IMPORTED_LOCATION "${MAYAUSDAPI_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MAYAUSD_INCLUDE_DIR}"
)

message(STATUS "MayaUsd include dir : ${MAYAUSD_INCLUDE_DIR}")
message(STATUS "MayaUsd library     : ${MAYAUSD_LIBRARY}")
message(STATUS "MayaUsdAPI library  : ${MAYAUSDAPI_LIBRARY}")
message(STATUS "MayaUsd Version     : ${MayaUsd_VERSION}")