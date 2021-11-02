## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(anari)
if(NOT WIN32)
  add_subdirectory(anari_debug)
endif()
add_subdirectory(anari_test_scenes)
add_subdirectory(anari_utilities)
