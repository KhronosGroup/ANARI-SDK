## Copyright 2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(hdanari_sdr LANGUAGES CXX)

project_add_library(SHARED
  debugCodes.cpp
  mdlDiscoveryPlugin.cpp
  mdlNodes.cpp
  mdlParserPlugin.cpp
  tokens.cpp
)

project_link_libraries(
  PRIVATE hdanari_mdl MDL_SDK::MDL_SDK
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

if (OPENUSD_PLUGIN_INSTALL_PREFIX)
  install(
    FILES
      ${PLUGINFO_PATH}
    DESTINATION
      ${OPENUSD_PLUGIN_INSTALL_PREFIX}/${PROJECT_NAME}/resources
  )

  install(
    TARGETS
      ${PROJECT_NAME}
    LIBRARY
      DESTINATION ${OPENUSD_PLUGIN_INSTALL_PREFIX}
    RUNTIME
      DESTINATION ${OPENUSD_PLUGIN_INSTALL_PREFIX}
    ARCHIVE
      DESTINATION ${OPENUSD_PLUGIN_INSTALL_PREFIX}
  )
endif()
