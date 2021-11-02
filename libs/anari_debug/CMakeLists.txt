## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_debug LANGUAGES CXX)

add_library(${PROJECT_NAME} SHARED DebugDevice.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE anari anari_utilities)

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
