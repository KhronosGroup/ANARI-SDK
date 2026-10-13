// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <anari/anari.h>
#include <pxr/base/gf/range3f.h>
#include <cstring>
#include <string_view>

namespace anari_hydra {

using Bounds = pxr::GfRange3f;

// Cached queries never wait. WAIT observes committed state on the worker,
// without authoring a mirror or requiring a render.
template <typename Scene>
bool QueryBounds(Scene &scene, Bounds &cached, const std::string_view &name,
    ANARIDataType type, void *ptr, uint64_t size, uint32_t flags)
{
  if (name != "bounds" || type != ANARI_FLOAT32_BOX3 || !ptr
      || size < 6 * sizeof(float))
    return false;
  if (flags == ANARI_WAIT)
    cached = scene.bounds();
  float values[6];
  for (int i = 0; i < 3; ++i) {
    values[i] = cached.GetMin()[i];
    values[i + 3] = cached.GetMax()[i];
  }
  std::memcpy(ptr, values, sizeof(values));
  return true;
}

} // namespace anari_hydra
