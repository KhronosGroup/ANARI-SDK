// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"

namespace hecore {

Renderer::Renderer(HeCoreDeviceGlobalState *s) : Object(ANARI_RENDERER, s) {}

Renderer *Renderer::createInstance(
    std::string_view /* subtype */, HeCoreDeviceGlobalState *s)
{
  return new Renderer(s);
}

void Renderer::commitParameters()
{
  m_background = getParam<float4>("background", float4(float3(0.f), 1.f));
}

float4 Renderer::background() const
{
  return m_background;
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Renderer *);
