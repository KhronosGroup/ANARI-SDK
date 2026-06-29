// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The CTS core is an internal static library. Its headers are shared only by
// the core's in-tree executable and tests, so its interface needs no dynamic
// import/export annotation.
#define ANARI_CTS_CORE_INTERFACE
