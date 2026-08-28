cmake_minimum_required(VERSION 3.17...4.0)

project(OVRTX_MAYA_PLUGIN VERSION 0.1.0 LANGUAGES CXX)

# User-configurable path to OpenUSD installation directory
set(USD_INSTALL_PREFIX "<path_to_openusd_install_directory>" CACHE PATH "OpenUSD Install Location")

# User-configurable path to MayaUSD installation directory
set(MAYAUSD_INSTALL_LOCATION "<path_to_mayausd_install_directory>" CACHE PATH "MayaUSD Install Location")

# User-configurable path to Maya installation directory
set(MAYA_INSTALL_LOCATION "<path_to_maya_install_directory>" CACHE PATH "Maya Install Location")

# User-configurable path to QT installation directory
set(QT_LOCATION "<path_to_qt_install_directory>" CACHE PATH "Qt Install Location")

list(APPEND CMAKE_PREFIX_PATH
    ${USD_INSTALL_PREFIX}
    ${QT_LOCATION}
)

# Add custom CMake modules directory to module search path
set(CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
)

#------------------------------------------------------------------------------
# Packages
#------------------------------------------------------------------------------

# OpenUSD
find_package(pxr CONFIG REQUIRED)
if(DEFINED PXR_VERSION)
    math(EXPR USD_MAJOR "${PXR_VERSION} / 10000")
    math(EXPR USD_TEMP  "${PXR_VERSION} % 10000")
    math(EXPR USD_MINOR "${USD_TEMP} / 100")
    math(EXPR USD_PATCH "${USD_TEMP} % 100")

    set(USD_VERSION_STRING "${USD_MAJOR}.${USD_MINOR}.${USD_PATCH}")
    message(STATUS "Using OpenUSD ${USD_VERSION_STRING}")
else()
    message(FATAL_ERROR "OpenUSD not found or PXR_VERSION not defined!")
endif()

# MayaUSD
find_package(MayaUSD REQUIRED)
if(MayaUSD_FOUND)
    message(STATUS "Using MayaUsd ${MayaUsd_VERSION}")
endif()

# UFE
find_package(UFE REQUIRED)
if(UFE_FOUND)
    message(STATUS "Using UFE ${UFE_VERSION}")
endif()

# Maya
find_package(Maya REQUIRED)
if(Maya_FOUND)
    message(STATUS "Using Maya ${MAYA_VERSION}")
endif()

# Qt
find_package(Qt6 COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Concurrent REQUIRED)
if(Qt6_FOUND)
    message(STATUS "Using Qt ${Qt6_VERSION}")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

#------------------------------------------------------------------------------
# Includes
#------------------------------------------------------------------------------
include(ovrtx)

#------------------------------------------------------------------------------
# Subdirs
#------------------------------------------------------------------------------
add_subdirectory(source)
