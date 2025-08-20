## Copyright 2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(hdanari_mdl LANGUAGES C CXX)

project_add_library(SHARED
  mdlRegistry.cpp
)

project_link_libraries(
  PUBLIC MDL_SDK::MDL_SDK
  ${PXR_LIBRARIES}
)

project_include_directories(
  PUBLIC ..
)

set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES PUBLIC_HEADER "api.h;mdlRegistry.h"
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
    PUBLIC_HEADER
      DESTINATION include/hdanari/mdl
  )