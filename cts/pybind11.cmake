## Copyright 2023-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(BUILD_TESTING  OFF)
set(PYBIND11_TEST OFF)
set(DOWNLOAD_GTEST  OFF)

anari_sdk_fetch_project(
  NAME anari_cts_pybind11
  URL https://github.com/pybind/pybind11/archive/refs/tags/v2.10.0.zip
  MD5 9eeed92aa1d7f018bbec4bcc22d4593b
  ADD_SUBDIR
)
