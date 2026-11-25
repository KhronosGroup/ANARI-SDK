# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

cmake_minimum_required(VERSION 3.18)

project(BSDLPackageImport LANGUAGES CXX)

find_package(BSDL REQUIRED CONFIG)

add_executable(bsdl_package_import main.cpp)
target_link_libraries(bsdl_package_import PRIVATE BSDL::BSDL)
