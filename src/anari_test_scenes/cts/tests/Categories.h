// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Catalog.h"

// Per-category Test registration. Each category lives in its own translation
// unit (geometry.cpp, material.cpp, ...) to keep the migrated corpus navigable;
// registerBuiltinTests() calls them all.

namespace anari {
namespace cts {

void registerGeometryTests(Catalog &catalog);
void registerMaterialTests(Catalog &catalog);
void registerSamplerTests(Catalog &catalog);
void registerLightTests(Catalog &catalog);
void registerCameraTests(Catalog &catalog);
void registerFrameTests(Catalog &catalog);
void registerRendererTests(Catalog &catalog);
void registerInstanceTests(Catalog &catalog);
void registerVolumeTests(Catalog &catalog);

} // namespace cts
} // namespace anari
