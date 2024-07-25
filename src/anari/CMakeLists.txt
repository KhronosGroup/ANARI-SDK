## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

option(ANARI_FRONTEND_CATCH_EXCEPTIONS "Have libanari catch all exceptions" ON)
mark_as_advanced(ANARI_FRONTEND_CATCH_EXCEPTIONS)

project_add_library(
  anari_cpp_linalg_defs.cpp
  anari_cpp_std_defs.cpp
  API.cpp
  DeviceImpl.cpp
  LibraryImpl.cpp
  test_c_header_compilation.c
)

if (NOT BUILD_SHARED_LIBS)
  project_compile_definitions(PUBLIC -DANARI_STATIC_DEFINE)
endif()

if (WIN32)
  project_compile_definitions(PUBLIC -D_USE_MATH_DEFINES)
endif()

if (ANARI_FRONTEND_CATCH_EXCEPTIONS)
  project_compile_definitions(PRIVATE -DANARI_FRONTEND_CATCH_EXCEPTIONS)
endif()

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${GENERATED_HEADER_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include/anari>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

project_link_libraries(PUBLIC ${CMAKE_DL_LIBS})

## Create version header

configure_file(anari_sdk_version.h.in include/anari/frontend/anari_sdk_version.h)

## Install library + targets

set_target_properties(${PROJECT_NAME}
  PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS ${PROJECT_NAME}
  EXPORT anari_Exports
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    NAMELINK_SKIP
  # on Windows put the dlls into bin
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  # ... and the import lib into the devel package
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(TARGETS anari
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    NAMELINK_ONLY
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

## Install headers

install(
  DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/anari
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/include/anari/frontend/anari_sdk_version.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/anari/frontend
)

