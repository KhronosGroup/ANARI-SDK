## Copyright 2023-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_test_scenes_kokkos LANGUAGES C CXX)

if (BUILD_SHARED_LIBS)
  # Kokkos does not export symbols on it's own causing
  # fatal error LNK1181: cannot open input file 'kokkoscore.lib'
  set(_push_CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ${CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS})
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif ()

# Enable threads execution space
set(Kokkos_ENABLE_THREADS ON CACHE STRING "")

anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/kokkos/kokkos/releases/download/4.5.01/kokkos-4.5.01.zip
  MD5 8d5d14567216760607bd393aaa04a012
  ADD_SUBDIR
)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ${_push_CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS})
