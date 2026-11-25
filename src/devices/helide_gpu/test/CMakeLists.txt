## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# Renders a few small scenes through the device and checks their pixels; the
# images are written to the build directory as smoke_*.png. Needs a GPU, so it
# is not part of the SDK's CI.

if (STANDALONE_SOURCE_TREE)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../../external/stb_image
    stb_image)
endif()

add_executable(helide_gpu_smoke_test smoke_test.cpp)
target_link_libraries(helide_gpu_smoke_test PRIVATE anari::anari stb_image)
add_dependencies(helide_gpu_smoke_test ${PROJECT_NAME})

add_test(NAME helide_gpu::smoke_test
  COMMAND helide_gpu_smoke_test helide_gpu
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# The device loads SDL3 at runtime; use the one it was built against.
set_tests_properties(helide_gpu::smoke_test PROPERTIES
  ENVIRONMENT "HELIDE_GPU_SDL3_LIBRARY=$<TARGET_FILE:SDL3::SDL3>")
