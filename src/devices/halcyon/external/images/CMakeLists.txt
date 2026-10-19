## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# The image file readers of the pathtracer's texture system
# (renderer/pathtracer/ImageFile.*; docs/adr/0005-pathtracer-texture-
# system.md, as amended at its acceptance): stb_image (PNG, JPEG, Radiance
# HDR, TGA, BMP, binary PNM) and tinyexr (OpenEXR) with miniz, fetched at
# configure time. ImageFile.cpp compiles their implementations; this target
# gives their headers and builds miniz, whose symbols stay out of whatever
# links it (--exclude-libs).

project(halcyon_image_loaders LANGUAGES C CXX)

# stb_image 2.30 (stb's master at the time; stb has no releases)
anari_sdk_fetch_project(
  NAME halcyon_stb
  URL "https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.zip"
  MD5 cf5324aaae339533d459bef199c266dd
)

# tinyexr 1.0.13, the single-header C++ version, which brings miniz
anari_sdk_fetch_project(
  NAME halcyon_tinyexr
  URL "https://github.com/syoyo/tinyexr/archive/refs/tags/v1.0.13.zip"
  MD5 b8c6b3149f422d3a09dec50fbbf629b3
)

add_library(halcyon_miniz STATIC ${halcyon_tinyexr_LOCATION}/deps/miniz/miniz.c)
target_include_directories(halcyon_miniz SYSTEM PUBLIC
  ${halcyon_tinyexr_LOCATION}/deps/miniz)
set_target_properties(halcyon_miniz PROPERTIES
  C_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON)

add_library(halcyon_image_loaders INTERFACE)
target_include_directories(halcyon_image_loaders SYSTEM INTERFACE
  ${halcyon_stb_LOCATION}
  ${halcyon_tinyexr_LOCATION})
target_link_libraries(halcyon_image_loaders INTERFACE halcyon_miniz)
if (UNIX AND NOT APPLE)
  target_link_options(halcyon_image_loaders INTERFACE
    "LINKER:--exclude-libs,$<TARGET_LINKER_FILE_NAME:halcyon_miniz>")
endif()
