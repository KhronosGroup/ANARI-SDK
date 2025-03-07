## Copyright 2021-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## NOTE: DO NOT USE THIS MODULE FILE!!! This CMake module sets up fake imported
##       targets so the examples can be written the same when built in either
##       the SDK source tree or externally using an SDK install. The ANARI-SDK
##       installs CMake target exports (anariConfig.cmake) that should be used
##       directly.

if (TARGET anari::anari)
  return()
endif()

set(ANARI_LOCAL_TARGETS
  anari_headers
  anari_backend
  anari
  anari_static
  anari_test_scenes
  helium
)

foreach(TARGET ${ANARI_LOCAL_TARGETS})
  add_library(anari::${TARGET} INTERFACE IMPORTED)
  set_target_properties(anari::${TARGET} PROPERTIES
    INTERFACE_LINK_LIBRARIES
      "${TARGET}"
  )
endforeach()
