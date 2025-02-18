## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

if (ANARI_LIBRARY_ONLY)
  return()
endif()
   
add_subdirectory(unit)
add_subdirectory(render)
