## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

option(BUILD_DEBUG_DEVICE "Build debug device layer" ON)
mark_as_advanced(BUILD_DEBUG_DEVICE)
if (BUILD_DEBUG_DEVICE)
  add_subdirectory(debug)
endif()

option(BUILD_SINK_DEVICE "Build the sink device" ON)
mark_as_advanced(BUILD_SINK_DEVICE)
if (BUILD_SINK_DEVICE)
  add_subdirectory(sink)
endif()

option(BUILD_HELIDE_DEVICE "Build example 'helide' device" ON)
if (BUILD_HELIDE_DEVICE)
  add_subdirectory(helide)
endif()

option(BUILD_REMOTE_DEVICE "Build 'remote' TCP passthrough device" OFF)
if (BUILD_REMOTE_DEVICE)
  add_subdirectory(remote)
endif()
