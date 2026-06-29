// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Catalog.h"
#include "Export.h"

namespace anari {
namespace cts {

// Register the built-in conformance Tests into a catalog.
ANARI_CTS_CORE_INTERFACE void registerBuiltinTests(Catalog &catalog);

} // namespace cts
} // namespace anari
