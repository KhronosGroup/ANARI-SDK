## Copyright 2024 The Khronos Group
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

project(hdAnari)

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

project(hdanari_rd LANGUAGES CXX)

project_add_library(SHARED
  anariExtensionUtilityImpl.cpp
  anariTokens.cpp
  debugCodes.cpp
  geometry.cpp
  instancer.cpp
  material.cpp
  material/matte.cpp
  material/physicallyBased.cpp
  material/textureLoader.cpp
  material/usdPreviewSurfaceConverter.cpp
  materialTokens.cpp
  materialTokens.h
  mesh.cpp
  meshUtil.cpp
  points.cpp
  renderBuffer.cpp
  renderDelegate.cpp
  rendererPlugin.cpp
  renderPass.cpp
  sampler.cpp
)

option(USE_INSTANCE_ARRAYS "Toggle on the use of instance arrays" OFF)
if (USE_INSTANCE_ARRAYS)
  target_compile_definitions(${PROJECT_NAME} PRIVATE "USE_INSTANCE_ARRAYS=1")
else()
  target_compile_definitions(${PROJECT_NAME} PRIVATE "USE_INSTANCE_ARRAYS=0")
endif()

set_target_properties(${PROJECT_NAME}
PROPERTIES
  # The other libs in the plugin dir have no "lib" prefix, so let's match this
  PREFIX ""
)

project_link_libraries(PUBLIC anari::anari ${PXR_LIBRARIES})

## Configure plugInfo file ##

set(PLUGINFO_PATH "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")

file(READ plugInfo.json.in PLUGINFO)
file(
  GENERATE
  OUTPUT
    ${PLUGINFO_PATH}
  CONTENT
    ${PLUGINFO}
)

## Installation ##

set(OPENUSD_INSTALL_PREFIX
  "${USD_DIR}/plugin/usd"
  CACHE
  PATH
  "OpenUSD location to install hdAnari plugin"
)

if (OPENUSD_INSTALL_PREFIX)
  install(
    FILES
      ${PLUGINFO_PATH}
    DESTINATION
      ${OPENUSD_INSTALL_PREFIX}/${PROJECT_NAME}/resources
  )

  install(
    TARGETS
      ${PROJECT_NAME}
    LIBRARY
      DESTINATION ${OPENUSD_INSTALL_PREFIX}
    RUNTIME
      DESTINATION ${OPENUSD_INSTALL_PREFIX}
    ARCHIVE
      DESTINATION ${OPENUSD_INSTALL_PREFIX}
  )
endif()
