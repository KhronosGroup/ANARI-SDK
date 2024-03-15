## Copyright 2023-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

include(FetchContent)

function(anari_sdk_fetch_project)
  cmake_parse_arguments(FETCH_SOURCE
    "ADD_SUBDIR"   # options
    "NAME;URL;MD5" # single-arg options
    ""             # multi-arg options
    ${ARGN}
  )

  if (FETCH_SOURCE_MD5)
    set(FETCH_SOURCE_MD5_COMMAND URL_MD5 ${FETCH_SOURCE_MD5})
  endif()

  set(SOURCE ${CMAKE_BINARY_DIR}/deps/source/${FETCH_SOURCE_NAME})

  FetchContent_Populate(${FETCH_SOURCE_NAME}
    URL ${FETCH_SOURCE_URL}
    DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}/.anari_deps/${FETCH_SOURCE_NAME}
    ${FETCH_SOURCE_MD5_COMMAND}
    SOURCE_DIR ${SOURCE}
  )

  set("${FETCH_SOURCE_NAME}_LOCATION" ${SOURCE} PARENT_SCOPE)

  if (FETCH_SOURCE_ADD_SUBDIR)
    add_subdirectory(
      ${SOURCE}
      ${CMAKE_BINARY_DIR}/deps/build/${FETCH_SOURCE_NAME}
    )
  endif()
endfunction()
