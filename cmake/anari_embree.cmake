## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# anari_embree_target(): defines the anari_embree target, which Embree-based
# devices (helide, halcyon) link for Embree and for the tasking system behind
# anari_embree::parallel_for() (anari_embree/include/anari_embree/
# parallel_for.h), unless it already exists. See anari_embree/CMakeLists.txt
# for how Embree is found or built.
#
# The SDK's own build includes this file, and so does anariConfig.cmake, so a
# device calls anari_embree_target() the same way built in the SDK's tree or
# alone against an installed SDK.
#
# Internal to the SDK's devices: it meets their private need for Embree and is
# not a way for downstream projects to get Embree, so it may change or go
# away without notice.

set(ANARI_EMBREE_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/anari_embree)

function(anari_embree_target)
  if (TARGET anari_embree)
    return()
  endif()
  add_subdirectory(${ANARI_EMBREE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/anari_embree EXCLUDE_FROM_ALL)
endfunction()
