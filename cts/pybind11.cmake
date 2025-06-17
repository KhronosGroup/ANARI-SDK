## Copyright 2023-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(BUILD_TESTING  OFF)
set(PYBIND11_TEST OFF)
set(DOWNLOAD_GTEST  OFF)

anari_sdk_fetch_project(
  NAME anari_cts_pybind11
  URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.zip
  MD5 671deeeaccfccd7c0389e43237c71cf3
  ADD_SUBDIR
)
