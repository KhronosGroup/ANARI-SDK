// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"

#include "Renderer_ispc.h"
#include "anari_library_halcyon_queries.h"
#include "array/Array1D.h"
#include "pathtracer/PathTracer.h"
#include "preview/Preview.h"
// std
#include <algorithm>

namespace halcyon {

RendererFrameState::~RendererFrameState() = default;

bool RendererFrameState::getProperty(
    const std::string_view &, ANARIDataType, void *, uint64_t, uint32_t)
{
  return false;
}

Renderer::Renderer(HalcyonDeviceGlobalState *s)
    : Object(ANARI_RENDERER, s, ispc::sizeof_Renderer())
{
  // helium only finalizes objects with changed parameters, so write the
  // defaults now for renderers that never get any
  updateISPC();
}

Renderer::~Renderer() = default;

Renderer *Renderer::createInstance(
    std::string_view subtype, HalcyonDeviceGlobalState *s)
{
  if (subtype == "quality")
    return new PathTracer(s);
  else if (subtype == "preview" || subtype == "default")
    return new Preview(s);
  else
    return (Renderer *)new UnknownObject(
        ANARI_RENDERER, s, ispc::sizeof_Renderer());
}

void Renderer::commitParameters()
{
  m_background = getParam<float4>("background", float4(float3(0.f), 1.f));
  m_backgroundImage = getParamObject<Array2D>("background");
  m_taskGrainSize.x =
      std::clamp(getParam<int32_t>("taskGrainSizeWidth", 4), 1, 128);
  m_taskGrainSize.y =
      std::clamp(getParam<int32_t>("taskGrainSizeHeight", 4), 1, 128);
}

void Renderer::finalize()
{
  m_backgroundImageTexels = toAttributeValues(m_backgroundImage.get());
  m_backgroundImageSize =
      m_backgroundImageTexels.empty() ? uint2(0u) : m_backgroundImage->size();
  updateISPC();
}

bool Renderer::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (name == "extension" && type == ANARI_STRING_LIST) {
    helium::writeToVoidP(ptr,
        query_object_info(ANARI_RENDERER,
            extensionSubtype(),
            "extension",
            ANARI_STRING_LIST));
    return true;
  }
  return Object::getProperty(name, type, ptr, size, flags);
}

void Renderer::updateISPC()
{
  // no image (including a 0-sized one) falls back to the color
  const bool hasImage = !m_backgroundImageTexels.empty();
  ispc::Renderer_setBackground(getIE(),
      &m_background.x,
      hasImage ? (const float *)m_backgroundImageTexels.data() : nullptr,
      m_backgroundImageSize.x,
      m_backgroundImageSize.y);
}

bool Renderer::wantsRender(const RendererFrameState *, bool changed) const
{
  return changed;
}

std::unique_ptr<RendererFrameState> Renderer::newFrameState() const
{
  return nullptr;
}

int2 Renderer::taskGrainSize() const
{
  return m_taskGrainSize;
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Renderer *);
