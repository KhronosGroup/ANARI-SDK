## Copyright 2023 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_viewer_nfd LANGUAGES CXX)

set(BUILD_SHARED_LIBS OFF)
anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/btzy/nativefiledialog-extended/archive/refs/tags/v1.0.3.zip
  MD5 7fd07c47f60a6db4798c705d075e610f
  ADD_SUBDIR
)

project_add_library(INTERFACE)
project_link_libraries(INTERFACE nfd)
