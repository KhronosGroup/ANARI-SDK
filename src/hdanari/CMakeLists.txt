## Copyright 2024-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

set(CMAKE_INSTALL_MESSAGE LAZY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

find_package(USD REQUIRED NAMES pxr)
find_package(anari REQUIRED)
if(LINUX)
  # MaterialX and USD might depend on X11 and OpenGL, but they
  # fail to declare that and directly use the related
  # targets. Try and find those packages as optional
  # so they don't actually break the build if missing.
  find_package(X11)
  find_package(OpenGL)
endif()

if(GCC OR CLANG)
  project_add_compile_options(-Wl,--no-undefined)
endif()

set(OPENUSD_PLUGIN_INSTALL_PREFIX
  "${USD_DIR}/plugin/usd"
  CACHE
  PATH
  "OpenUSD location to install USD plugin"
)

option(HDANARI_ENABLE_MDL "Enable MDL support for HdAnari" OFF)
if(HDANARI_ENABLE_MDL)
  find_package(MDL_SDK REQUIRED)
  add_subdirectory(mdl)
  option(HDANARI_ENABLE_SDR "Build OpenUSD SDR plugin (MDL material discovery suppport for standalone HdAnari)" ON)
  if (HDANARI_ENABLE_SDR)
    add_subdirectory(sdr)
  endif()
endif()
add_subdirectory(rd)

# Conditionally install a main plugInfo.json file
# Rational is the following: If OPENUSD_INSTALL_DIR is set to the main OpenUSD folder
# this very file will exist and have a meaningful content. This will enable our plugins to
# be found in a standard OpenUSD plugin hierarchy.
# If not, then we create our own file that will expose all our pluginis from a single search path.
# This path will have to be added to the PXR_PLUGINPATH_NAME variable.
if(NOT EXISTS "${OPENUSD_PLUGIN_INSTALL_PREFIX}/plugInfo.json")
  install(FILES plugInfo.json DESTINATION "${OPENUSD_PLUGIN_INSTALL_PREFIX}/")
endif()

