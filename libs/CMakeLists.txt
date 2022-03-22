## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(anari)
add_subdirectory(anari_utilities)
if(BUILD_TESTING OR BUILD_EXAMPLES)
  add_subdirectory(anari_test_scenes)
endif()