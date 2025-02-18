## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

if (ANARI_LIBRARY_ONLY)
  add_subdirectory(anari)
  add_subdirectory(helium)
  return()
endif()

add_subdirectory(anari)
add_subdirectory(anari_test_scenes)
add_subdirectory(anari_viewer)
add_subdirectory(helium)
add_subdirectory(devices)

option(BUILD_HDANARI "Build OpenUSD Hydra delegate plugin" OFF)
if (BUILD_HDANARI)
  add_subdirectory(hdanari)
endif()
