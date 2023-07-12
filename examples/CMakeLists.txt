## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# Example applications which use the ANARI API
add_subdirectory(simple)

option(BUILD_VIEWER "Build interactive viewer app (requires GLFW)" OFF)

if (BUILD_VIEWER)
  add_subdirectory(viewer)
endif()
