## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# Example applications which use the ANARI API
add_subdirectory(simple)
add_subdirectory(generated_device_frontend)
if (BUILD_VIEWER)
  add_subdirectory(viewer)
endif()
