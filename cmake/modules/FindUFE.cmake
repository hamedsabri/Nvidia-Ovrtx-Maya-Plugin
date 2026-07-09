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
# FindUFE.cmake
#
# This module defines imported targets:
#   UFE::ufe
#
# Once you call:
#   find_package(UFE REQUIRED)
#
# You do NOT need to manually add include directories or link directories.
# Simply link your target with:
#
#   target_link_libraries(yourTargetName
#       PRIVATE
#           UFE::ufe
#   )
#
# This will automatically propagate the correct include paths and libraries.

# Hint locations – prioritize user/environment variables
set(_ufe_hint_paths
    $ENV{MAYA_INSTALL_LOCATION}
    ${MAYA_INSTALL_LOCATION}
)

# ------------------------------------------------------------------
# Find headers
# ------------------------------------------------------------------
find_path(UFE_INCLUDE_DIR
    NAMES ufe/versionInfo.h ufe/ufe.h
    HINTS ${_ufe_hint_paths}
    PATH_SUFFIXES
        devkit/ufe/include
        include
    DOC "UFE include directory"
)

# ------------------------------------------------------------------
# Extract version from ufe.h
# ------------------------------------------------------------------
if(UFE_INCLUDE_DIR AND EXISTS "${UFE_INCLUDE_DIR}/ufe/ufe.h")
    # Parse the file and get the three lines that have the version info.
    file(STRINGS
        "${UFE_INCLUDE_DIR}/ufe/ufe.h"
        _ufe_vers
        REGEX "#define[ ]+(UFE_MAJOR_VERSION|UFE_MINOR_VERSION|UFE_PATCH_LEVEL)[ ]+[0-9]+$")

    # Then extract the number from each one.
    foreach(_ufe_tmp ${_ufe_vers})
        if(_ufe_tmp MATCHES "#define[ ]+(UFE_MAJOR_VERSION|UFE_MINOR_VERSION|UFE_PATCH_LEVEL)[ ]+([0-9]+)$")
            set(${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
        endif()
    endforeach()
    set(UFE_VERSION ${UFE_MAJOR_VERSION}.${UFE_MINOR_VERSION}.${UFE_PATCH_LEVEL})
endif()

# ------------------------------------------------------------------
# Find library
# ------------------------------------------------------------------
find_library(UFE_LIBRARY
    NAMES ufe_${UFE_MAJOR_VERSION}
    HINTS ${_ufe_hint_paths}
    PATH_SUFFIXES
        devkit/ufe/lib
        lib
    DOC "UFE library"
)

# ------------------------------------------------------------------
# Standard handling
# ------------------------------------------------------------------
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UFE
    REQUIRED_VARS UFE_LIBRARY UFE_INCLUDE_DIR
    VERSION_VAR UFE_VERSION
)

mark_as_advanced(UFE_INCLUDE_DIR UFE_LIBRARY)

# ------------------------------------------------------------------
# Imported target (modern usage only)
# ------------------------------------------------------------------
if(UFE_FOUND AND NOT TARGET UFE::ufe)
    add_library(UFE::ufe UNKNOWN IMPORTED)
    set_target_properties(UFE::ufe PROPERTIES
        IMPORTED_LOCATION "${UFE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${UFE_INCLUDE_DIR}"
    )
endif()