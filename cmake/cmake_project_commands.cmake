## Copyright 2022-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

function(project_add_executable)
  add_executable(${PROJECT_NAME} ${ARGN})
endfunction()

function(project_add_library)
  add_library(${PROJECT_NAME} ${ARGN})
endfunction()

function(project_compile_definitions)
  target_compile_definitions(${PROJECT_NAME} ${ARGN})
endfunction()

function(project_compile_features)
  target_compile_features(${PROJECT_NAME} ${ARGN})
endfunction()

function(project_compile_options)
  target_compile_options(${PROJECT_NAME} ${ARGN})
endfunction()

function(project_include_directories)
  target_include_directories(${PROJECT_NAME} ${ARGN})
endfunction()

function(project_link_libraries)
  target_link_libraries(${PROJECT_NAME} ${ARGN})
endfunction()

function(project_link_directories)
  target_link_directories(${PROJECT_NAME} ${ARGN})
endfunction()

function(project_link_options)
  target_link_options(${PROJECT_NAME} ${ARGN})
endfunction()

function(project_precompiled_headers)
  target_precompiled_headers(${PROJECT_NAME} ${ARGN})
endfunction()

function(project_sources)
  target_sources(${PROJECT_NAME} ${ARGN})
endfunction()
