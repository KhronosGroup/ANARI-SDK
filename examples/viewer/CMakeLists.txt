## Copyright 2023 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anariViewer)
project_add_executable(main.cpp ui_layout.cpp)
project_link_libraries(PRIVATE anari_viewer)
