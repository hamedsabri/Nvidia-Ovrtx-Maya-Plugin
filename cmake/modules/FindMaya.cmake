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

# FindMaya.cmake
#
# This module defines imported targets:
#   Maya::Maya          # Links to the core set of Maya libraries (Foundation, OpenMaya, etc.)

# Individual library targets for finer control (optional)
#   Maya::OpenMaya      
#   Maya::OpenMayaAnim
#   Maya::OpenMayaFX
#   Maya::OpenMayaRender
#   Maya::OpenMayaUI
#   Maya::Foundation
#   Maya::Image
#   # Add more as needed
#
# Once you call:
#   find_package(Maya REQUIRED)
#
# You do NOT need to manually add include directories or link directories.
# Simply link your plugin target with:
#
#   target_link_libraries(yourPluginTarget
#       PRIVATE
#           Maya::Maya   # or individual targets like Maya::OpenMaya Maya::Foundation etc.
#   )
#
# This will automatically propagate the correct include paths, libraries, and platform-specific settings.

include(FindPackageHandleStandardArgs)

# Hints and common locations
set(_MAYA_HINTS
    $ENV{MAYA_INSTALL_LOCATION}
    ${MAYA_INSTALL_LOCATION}
)

# Allow user to specify Maya version if needed
set(MAYA_VERSION "" CACHE STRING "Maya version to search for (e.g. 2024, 2025)")

# Find include directory
find_path(MAYA_INCLUDE_DIR
    NAMES maya/MFn.h  # Common header in all Maya versions
    HINTS ${_MAYA_HINTS}
    PATH_SUFFIXES include devkit/include
)

# Find library directory (common Maya libs)
find_path(MAYA_LIBRARY_DIR
    NAMES OpenMaya.lib libOpenMaya.so
    HINTS ${_MAYA_HINTS}
    PATH_SUFFIXES lib
)

# Core Maya libraries (most plugins need these)
set(_MAYA_CORE_LIBS
    Foundation
    OpenMaya
    OpenMayaAnim
    OpenMayaFX
    OpenMayaRender
    OpenMayaUI
    Image
)

foreach(_lib ${_MAYA_CORE_LIBS})
    find_library(MAYA_${_lib}_LIBRARY
        NAMES ${_lib} lib${_lib}
        HINTS ${MAYA_LIBRARY_DIR}
        NO_DEFAULT_PATH
    )
    list(APPEND MAYA_LIBRARIES ${MAYA_${_lib}_LIBRARY})
endforeach()

# Version detection from header
set(MAYA_VERSION "UNKNOWN")
if(MAYA_INCLUDE_DIR)
    set(_mtypes_header "${MAYA_INCLUDE_DIR}/maya/MTypes.h")
    if(EXISTS "${_mtypes_header}")
        file(STRINGS "${_mtypes_header}" _version_line REGEX "^#define MAYA_API_VERSION")
        if(_version_line)
            string(REGEX REPLACE ".*MAYA_API_VERSION[ \t]+([0-9]+).*" "\\1" _api_version "${_version_line}")
            string(SUBSTRING "${_api_version}" 0 4 MAYA_VERSION_YEAR)
            set(MAYA_VERSION "${MAYA_VERSION_YEAR}")
        endif()
    endif()
endif()

find_package_handle_standard_args(
    Maya
    REQUIRED_VARS MAYA_INCLUDE_DIR MAYA_LIBRARY_DIR MAYA_LIBRARIES
    VERSION_VAR MAYA_VERSION
)

if(NOT Maya_FOUND)
    message(FATAL_ERROR "Autodesk Maya not found! Set MAYA_INSTALL_LOCATION or MAYA_VERSION if needed.")
    return()
endif()

# Single aggregated target: Maya::Maya (most common use case)
add_library(Maya::Maya INTERFACE IMPORTED)
set_target_properties(Maya::Maya PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${MAYA_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${MAYA_LIBRARIES}"
    INTERFACE_LINK_DIRECTORIES "${MAYA_LIBRARY_DIR}"
)

# Optional: Individual imported targets for finer-grained linking
foreach(_lib ${_MAYA_CORE_LIBS})
    if(MAYA_${_lib}_LIBRARY)
        add_library(Maya::${_lib} UNKNOWN IMPORTED)
        set_target_properties(Maya::${_lib} PROPERTIES
            IMPORTED_LOCATION "${MAYA_${_lib}_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MAYA_INCLUDE_DIR}"
        )
    endif()
endforeach()

# Status output
message(STATUS "Maya include dir : ${MAYA_INCLUDE_DIR}")
message(STATUS "Maya library dir : ${MAYA_LIBRARY_DIR}")
message(STATUS "Maya version     : ${MAYA_VERSION}")