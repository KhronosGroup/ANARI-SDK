## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(anari)
add_subdirectory(helium)
add_subdirectory(debug_device)
add_subdirectory(sink_device)

option(BUILD_HELIDE_DEVICE "Build example 'helide' device" ON)
if (BUILD_HELIDE_DEVICE)
  add_subdirectory(helide)
endif()

if(BUILD_TESTING OR BUILD_EXAMPLES)
  add_subdirectory(anari_test_scenes)
endif()

if(BUILD_EXAMPLES AND BUILD_VIEWER)
  add_subdirectory(anari_viewer)
endif()

option(BUILD_REMOTE_DEVICE "Build 'remote' TCP passthrough device" OFF)
if (BUILD_REMOTE_DEVICE)
  add_subdirectory(remote_device)
endif()
