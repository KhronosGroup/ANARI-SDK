// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BuiltinTests.h"
#include "tests/Categories.h"

namespace anari {
namespace cts {

void registerBuiltinTests(Catalog &catalog)
{
  registerGeometryTests(catalog);
  registerMaterialTests(catalog);
  registerSamplerTests(catalog);
  registerLightTests(catalog);
  registerCameraTests(catalog);
  registerFrameTests(catalog);
  registerRendererTests(catalog);
  registerInstanceTests(catalog);
  registerVolumeTests(catalog);
  registerGltfTests(catalog);
}

} // namespace cts
} // namespace anari
