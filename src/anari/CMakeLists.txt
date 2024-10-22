## Copyright 2021-2024 The Khronos Group
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
    DeviceImpl.cpp
    LibraryImpl.cpp
    test_c_header_compilation.c
  )

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

  project_link_libraries(PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

  set_target_properties(${PROJECT_NAME}
    PROPERTIES
      VERSION ${ANARI_SDK_VERSION}
      SOVERSION ${ANARI_SDK_VERSION_MAJOR}
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

  install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      NAMELINK_ONLY
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endmacro()

## Create shared + static frontend library targets ##

add_anari_frontend_library_target(SHARED)

project(anari_static VERSION ${ANARI_SDK_VERSION} LANGUAGES C CXX)
add_anari_frontend_library_target(STATIC)
project_compile_definitions(PUBLIC -DANARI_STATIC_DEFINE)

## Create version header ##

configure_file(anari_sdk_version.h.in include/anari/frontend/anari_sdk_version.h)

## Install headers ##

install(
  DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/anari
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/include/anari/frontend/anari_sdk_version.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/anari/frontend
)

