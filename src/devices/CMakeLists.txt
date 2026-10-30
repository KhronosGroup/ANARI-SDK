## Copyright 2021-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

option(BUILD_DEBUG_DEVICE "Build debug device layer" ON)
mark_as_advanced(BUILD_DEBUG_DEVICE)
if (BUILD_DEBUG_DEVICE)
  add_subdirectory(debug)
endif()

option(BUILD_SINK_DEVICE "Build the sink device" ON)
mark_as_advanced(BUILD_SINK_DEVICE)
if (BUILD_SINK_DEVICE)
  add_subdirectory(sink)
endif()

option(BUILD_CAPTURE_DEVICE "Build the capture device (snapshots committed state)" ON)
mark_as_advanced(BUILD_CAPTURE_DEVICE)
if (BUILD_CAPTURE_DEVICE)
  add_subdirectory(capture)
endif()

option(BUILD_HELIDE_DEVICE "Build example 'helide' device" ON)
if (BUILD_HELIDE_DEVICE)
  add_subdirectory(helide)
endif()

option(BUILD_HELIDE_GPU_DEVICE "Build example 'helide_gpu' device" OFF)
if (BUILD_HELIDE_GPU_DEVICE)
  add_subdirectory(helide_gpu)
endif()

option(BUILD_HALCYON_DEVICE "Build 'halcyon' device (Embree + ISPC; needs an ISPC compiler)" OFF)
if (BUILD_HALCYON_DEVICE)
  add_subdirectory(halcyon)
endif()

option(BUILD_HYDRA_DEVICE "Build 'hydra' device (OpenUSD Hydra render delegates; needs OpenUSD)" OFF)
if (BUILD_HYDRA_DEVICE)
  add_subdirectory(hydra)
endif()

option(BUILD_REMOTE_DEVICE "Build 'remote' TCP passthrough device" OFF)
if (BUILD_REMOTE_DEVICE)
  add_subdirectory(remote)
endif()
