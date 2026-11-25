# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

add_executable(bsdl_header_compile header_compile.cpp)
target_link_libraries(bsdl_header_compile PRIVATE BSDL::BSDL)
add_test(NAME bsdl_header_compile COMMAND bsdl_header_compile)

add_executable(bsdl_smoke smoke.cpp)
target_link_libraries(bsdl_smoke PRIVATE BSDL::BSDL)
add_test(NAME bsdl_smoke COMMAND bsdl_smoke)

add_test(NAME bsdl_generated_luts_exist
         COMMAND ${CMAKE_COMMAND}
                 "-DGENERATED_LUT_HEADERS=${BSDL_GENERATED_LUT_HEADERS}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/check_generated_luts.cmake")
