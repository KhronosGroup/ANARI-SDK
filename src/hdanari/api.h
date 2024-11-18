// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/base/arch/export.h>

#if defined(PXR_STATIC)
#define HDANARI_RD_API
#define HDANARI_RD_API_TEMPLATE_CLASS(...)
#define HDANARI_RD_API_TEMPLATE_STRUCT(...)
#define HDANARI_RD_LOCAL
#else
#if defined(hdanari_rd_EXPORTS)
#define HDANARI_RD_API ARCH_EXPORT
#define HDANARI_RD_API_TEMPLATE_CLASS(...)                                     \
  ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#define HDANARI_RD_API_TEMPLATE_STRUCT(...)                                    \
  ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#else
#define HDANARI_RD_API ARCH_IMPORT
#define HDANARI_RD_API_TEMPLATE_CLASS(...)                                     \
  ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#define HDANARI_RD_API_TEMPLATE_STRUCT(...)                                    \
  ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#endif
#define HDANARI_RD_LOCAL ARCH_HIDDEN
#endif
