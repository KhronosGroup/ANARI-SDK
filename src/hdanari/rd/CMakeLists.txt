## Copyright 2024-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(hdanari_rd LANGUAGES CXX)

project_add_library(SHARED
  anariExtensionUtilityImpl.cpp
  anariTokens.cpp
  debugCodes.cpp
  geometry.cpp
  instancer.cpp
  light.cpp
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
# Visual Studio compiler might kill some symbols (arch_ctor_<name>) used to initialize USD plugins at load time.
# Disabling that optimization for the renderPlugin file works around the issue.
set_source_files_properties(rendererPlugin.cpp PROPERTIES COMPILE_FLAGS $<$<CXX_COMPILER_ID:MSVC>:"/Zc:inline-">)

set_target_properties(${PROJECT_NAME}
PROPERTIES
  # The other libs in the plugin dir have no "lib" prefix, so let's match this
  PREFIX ""
)

project_link_libraries(PUBLIC anari::anari_static ${PXR_LIBRARIES})
if(HDANARI_ENABLE_MDL)
  project_sources(
    PRIVATE material/mdl.cpp
  )
  project_compile_definitions(
    PRIVATE HDANARI_ENABLE_MDL
  )
  project_link_libraries(PRIVATE hdanari_mdl)
endif()

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
