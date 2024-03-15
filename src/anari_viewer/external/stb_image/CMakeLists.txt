## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_viewer_stb_image LANGUAGES C)
project_add_library(STATIC stb_image_write.c stb_image.c)
project_include_directories(INTERFACE ${CMAKE_CURRENT_LIST_DIR})
