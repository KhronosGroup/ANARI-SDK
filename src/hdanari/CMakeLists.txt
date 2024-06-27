## Copyright 2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

set(CMAKE_INSTALL_MESSAGE LAZY)

project(hdAnari LANGUAGES C CXX)

find_package(USD REQUIRED NAMES pxr)
find_package(anari REQUIRED)

project_add_library(SHARED
  anariExtensionUtilityImpl.cpp
  debugCodes.cpp
  instancer.cpp
  material.cpp
  mesh.cpp
  renderBuffer.cpp
  renderDelegate.cpp
  rendererPlugin.cpp
  renderPass.cpp
  sampler.cpp
)

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
