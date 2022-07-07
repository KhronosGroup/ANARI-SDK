## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../cmake)

# Example device implementation, usable with all example apps below
add_subdirectory(example_device)

# Example applications which use the ANARI API
add_subdirectory(simple)
add_subdirectory(generated_device_frontend)
option(BUILD_VIEWER "Build interactive viewer app (requires GLFW)" OFF)
if (BUILD_VIEWER)
  add_subdirectory(viewer)
endif()
