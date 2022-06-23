// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef _WIN32
#ifdef EXAMPLE_DEVICE_STATIC_DEFINE
#define EXAMPLE_DEVICE_INTERFACE
#else
#ifdef anari_library_example_EXPORTS
#define EXAMPLE_DEVICE_INTERFACE __declspec(dllexport)
#else
#define EXAMPLE_DEVICE_INTERFACE __declspec(dllimport)
#endif
#endif
#elif defined __GNUC__
#define EXAMPLE_DEVICE_INTERFACE __attribute__((__visibility__("default")))
#else
#define EXAMPLE_DEVICE_INTERFACE
#endif
