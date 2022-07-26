## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

if (TARGET stb_image)
  return()
endif()

add_library(stb_image STATIC stb_image_write.c stb_image.c)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_LIST_DIR})
