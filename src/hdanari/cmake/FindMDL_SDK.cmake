# Copyright 2025 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

if (TARGET MDL_SDK::MDL_SDK)
  return()
endif()

find_path(MDL_SDK_ROOT NAMES "include/mi/mdl_sdk.h" PATHS ${MDL_SDK_PATH} ENV MDL_SDK_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MDL_SDK DEFAULT_MSG MDL_SDK_ROOT)

set(MDL_SDK_INCLUDE_DIR ${MDL_SDK_ROOT}/include)
set(MDL_SDK_INCLUDE_DIRS ${MDL_SDK_ROOT}/include)
mark_as_advanced(MDL_SDK_INCLUDE_DIR MDL_SDK_INCLUDE_DIRS)

add_library(MDL_SDK::MDL_SDK INTERFACE IMPORTED)
target_include_directories(MDL_SDK::MDL_SDK INTERFACE ${MDL_SDK_INCLUDE_DIR})
