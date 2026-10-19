# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

foreach(header IN LISTS GENERATED_LUT_HEADERS)
    if(NOT EXISTS "${header}")
        message(FATAL_ERROR "Missing generated LUT header: ${header}")
    endif()
endforeach()
