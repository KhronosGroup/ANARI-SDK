## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(debug)
add_subdirectory(sink)

option(BUILD_HELIDE_DEVICE "Build example 'helide' device" ON)
if (BUILD_HELIDE_DEVICE)
  add_subdirectory(helide)
endif()

option(BUILD_REMOTE_DEVICE "Build 'remote' TCP passthrough device" OFF)
if (BUILD_REMOTE_DEVICE)
  add_subdirectory(remote)
endif()
