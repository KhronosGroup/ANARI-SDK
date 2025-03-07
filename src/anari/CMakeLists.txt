## Copyright 2021-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

option(ANARI_FRONTEND_CATCH_EXCEPTIONS "Have libanari catch all exceptions" ON)
mark_as_advanced(ANARI_FRONTEND_CATCH_EXCEPTIONS)

# Some platforms simply don't function without linking against Threads::Threads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

macro(add_anari_frontend_library_target LIB_TYPE)
  project_add_library(${LIB_TYPE}
    anari_cpp_linalg_defs.cpp
    anari_cpp_std_defs.cpp
    API.cpp
    LibraryImpl.cpp
    test_c_header_compilation.c
  )
  if (ANARI_FRONTEND_CATCH_EXCEPTIONS)
    project_compile_definitions(PRIVATE -DANARI_FRONTEND_CATCH_EXCEPTIONS)
  endif()
  project_link_libraries(PUBLIC ${CMAKE_DL_LIBS} anari_headers anari_backend)
  set_target_properties(${PROJECT_NAME}
    PROPERTIES
      VERSION ${ANARI_SDK_VERSION}
      SOVERSION ${ANARI_SDK_VERSION_MAJOR}
  )
endmacro()

## Create auxiliary targets ##

project(anari_headers)
project_add_library(INTERFACE)
project_include_directories(
INTERFACE
  $<BUILD_INTERFACE:${GENERATED_HEADER_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include/anari>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if (WIN32)
  project_compile_definitions(INTERFACE -D_USE_MATH_DEFINES)
endif()

project(anari_backend)
project_add_library(STATIC DeviceImpl.cpp LibraryImpl.cpp)
project_link_libraries(PUBLIC Threads::Threads anari_headers)
project_compile_definitions(PRIVATE -Danari_EXPORTS)

## Create main shared + static library targets ##

project(anari)
add_anari_frontend_library_target(SHARED)

project(anari_static)
add_anari_frontend_library_target(STATIC)
project_compile_definitions(PUBLIC -DANARI_STATIC_DEFINE)

## Create version header ##

configure_file(anari_sdk_version.h.in include/anari/frontend/anari_sdk_version.h)

## Install ##

set(ANARI_TARGETS
  anari
  anari_static
  anari_backend
  anari_headers
)

install(
  TARGETS ${ANARI_TARGETS}
  EXPORT anari_Exports
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    NAMELINK_SKIP
  # on Windows put the dlls into bin
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  # ... and the import lib into the devel package
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
  TARGETS ${ANARI_TARGETS}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    NAMELINK_ONLY
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
  DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/anari
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/include/anari/frontend/anari_sdk_version.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/anari/frontend
)

