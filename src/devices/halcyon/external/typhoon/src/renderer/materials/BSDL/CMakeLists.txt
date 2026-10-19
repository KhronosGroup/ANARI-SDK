# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

cmake_minimum_required(VERSION 3.18)

project(BSDL
    VERSION 0.1.0
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(BSDL_BUILD_TESTS "Build BSDL tests" ${PROJECT_IS_TOP_LEVEL})

find_package(Imath REQUIRED CONFIG)
find_package(Threads REQUIRED)

include(bsdl.cmake)

# BSDL has small tables for spectral rendering in sRGB. Generate the ACEScg
# Jakob-Hanika table used by hdEmbree's lin_ap1_scene spectral path.
add_bsdl_library(BSDL SPECTRAL_COLOR_SPACES ACEScg)
add_library(BSDL_spectral_luts STATIC ${BSDL_LUTS_CPP})
target_link_libraries(BSDL_spectral_luts PUBLIC BSDL)
set_target_properties(BSDL_spectral_luts PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
add_library(BSDL::BSDL ALIAS BSDL)
file(MAKE_DIRECTORY "${BSDL_GENERATED_INCLUDE_DIR}/BSDL")

install(DIRECTORY include/BSDL
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT developer)
install(DIRECTORY ${BSDL_GENERATED_INCLUDE_DIR}/BSDL
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT developer)

install(TARGETS BSDL
        EXPORT BSDLTargets)

set(BSDL_CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/BSDL")
set(BSDL_VERSION_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/BSDLConfigVersion.cmake")
set(BSDL_PROJECT_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/BSDLConfig.cmake")

write_basic_package_version_file(
    ${BSDL_VERSION_CONFIG}
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BSDLConfig.cmake.in"
    "${BSDL_PROJECT_CONFIG}"
    INSTALL_DESTINATION "${BSDL_CONFIG_INSTALL_DIR}")

install(FILES
        "${BSDL_PROJECT_CONFIG}"
        "${BSDL_VERSION_CONFIG}"
        DESTINATION "${BSDL_CONFIG_INSTALL_DIR}"
        COMPONENT developer)

install(EXPORT BSDLTargets
        FILE BSDLTargets.cmake
        NAMESPACE BSDL::
        DESTINATION "${BSDL_CONFIG_INSTALL_DIR}"
        COMPONENT developer)

if(BSDL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
