## Copyright 2023-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(cat_cli11 LANGUAGES CXX)

anari_sdk_fetch_project(
  ADD_SUBDIR
  NAME ${PROJECT_NAME}
  URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.5.0.zip
  MD5 c96926a6336f69143fa52e2e85e1082c
)
