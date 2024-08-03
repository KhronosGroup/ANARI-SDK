## Copyright 2023-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_viewer_nfd LANGUAGES CXX)

set(BUILD_SHARED_LIBS OFF)
anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/btzy/nativefiledialog-extended/archive/refs/tags/v1.2.1.zip
  MD5 fe75247976f9a06d01218abb6f53ecca
  ADD_SUBDIR
)

project_add_library(INTERFACE)
project_link_libraries(INTERFACE nfd)
