// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef _WIN32
#ifdef DEBUG_DEVICE_STATIC_DEFINE
#define DEBUG_DEVICE_INTERFACE
#else
#ifdef anari_library_debug_EXPORTS
#define DEBUG_DEVICE_INTERFACE __declspec(dllexport)
#else
#define DEBUG_DEVICE_INTERFACE __declspec(dllimport)
#endif
#endif
#elif defined __GNUC__
#define DEBUG_DEVICE_INTERFACE __attribute__((__visibility__("default")))
#else
#define DEBUG_DEVICE_INTERFACE
#endif
