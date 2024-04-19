## Copyright 2022-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

anari_generate_queries(
  NAME cts_queries
  PREFIX cts
  CPP_NAMESPACE cts
  JSON_DEFINITIONS_FILE ${ANARI_CODE_GEN_ROOT}/devices/experimental_device.json
)
