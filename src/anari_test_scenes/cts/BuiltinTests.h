// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Catalog.h"
#include "anari_test_scenes_export.h"

namespace anari {
namespace cts {

// Register the built-in conformance Tests into a catalog. This is a small seed
// covering a couple of geometry tests so the runner and the `cts` tool are
// exercisable end to end; Phase 3 grows it into the full migrated corpus.
ANARI_TEST_SCENES_INTERFACE void registerBuiltinTests(Catalog &catalog);

} // namespace cts
} // namespace anari
