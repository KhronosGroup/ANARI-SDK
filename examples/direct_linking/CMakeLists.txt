## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_executable(anariTutorialDirectLink anariTutorialDirectLink.c)
target_link_libraries(anariTutorialDirectLink
PRIVATE
  anari_library_example
  stb_image
)
