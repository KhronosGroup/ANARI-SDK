## Copyright 2022-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## NOTE: DO NOT USE THIS MODULE FILE!!! This CMake module sets up fake imported
##       targets so the examples can be written the same when built in either
##       the SDK source tree or externally using an SDK install. The ANARI-SDK
##       installs CMake target exports (anariConfig.cmake) that should be used
##       directly.

if (TARGET anari::anari)
  return()
endif()

add_library(anari::anari INTERFACE IMPORTED)
set_target_properties(anari::anari PROPERTIES
  INTERFACE_LINK_LIBRARIES
    "anari"
)

add_library(anari::anari_test_scenes INTERFACE IMPORTED)
set_target_properties(anari::anari_test_scenes PROPERTIES
  INTERFACE_LINK_LIBRARIES
    "anari_test_scenes"
)

add_library(anari::helium INTERFACE IMPORTED)
set_target_properties(anari::helium PROPERTIES
  INTERFACE_LINK_LIBRARIES
    "helium"
)
