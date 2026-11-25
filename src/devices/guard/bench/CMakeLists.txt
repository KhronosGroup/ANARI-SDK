## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# guardBench: measures the guard device against the same device in-process and
# writes the cost table. Built by default, never run by ctest and never
# installed: its figures only mean something on a quiet machine.
#
#   guardBench out=<path>/overhead.md

add_executable(guardBench guardBench.cpp)
target_link_libraries(guardBench PRIVATE anari::anari_test_scenes)
target_compile_definitions(guardBench PRIVATE
  GUARD_BENCH_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}/.."
  GUARD_BENCH_BUILD_TYPE="$<CONFIG>"
  GUARD_BENCH_SDK_VERSION="${anari_VERSION}"
)
add_dependencies(guardBench anari_library_guard anariGuardHost)
