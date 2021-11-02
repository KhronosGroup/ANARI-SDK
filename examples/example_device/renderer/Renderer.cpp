// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"
// specific types
#include "AmbientOcclusion.h"
#include "Debug.h"
#include "RayDir.h"
#include "Raycast.h"

namespace anari {
namespace example_device {

ANARIParameter Renderer::g_parameters[] = {
    {"backgroundColor", ANARI_FLOAT32_VEC4},
    {"pixelSamples", ANARI_INT32},
    {NULL, ANARI_UNKNOWN}};

ParameterInfo Renderer::g_parameterinfos[] = {
    {"background color and alpha (RGBA)", false, {.v = vec4(1.f)}},
    {"samples per pixel", false, {1}}};

FactoryMapPtr<Renderer> Renderer::g_renderers;
void Renderer::init()
{
  if (g_renderers.get() != nullptr)
    return;

  g_renderers = std::make_unique<FactoryMap<Renderer>>();

  g_renderers->emplace("rayDir", []() -> Renderer * { return new RayDir; });
  g_renderers->emplace("raycast", []() -> Renderer * { return new Raycast; });
  g_renderers->emplace("debug", []() -> Renderer * { return new Debug; });
  g_renderers->emplace(
      "pathtracer", []() -> Renderer * { return new AmbientOcclusion; });
  g_renderers->emplace(
      "scivis", []() -> Renderer * { return new AmbientOcclusion; });
  g_renderers->emplace(
      "ao", []() -> Renderer * { return new AmbientOcclusion; });
  g_renderers->emplace(
      "default", []() -> Renderer * { return new AmbientOcclusion; });
}

Renderer *Renderer::createInstance(const char *type)
{
  init();

  auto *fcn = (*g_renderers)[type];

  if (fcn)
    return fcn();
  else
    throw std::runtime_error("could not create renderer");
}

int Renderer::pixelSamples() const
{
  return m_pixelSamples;
}

void Renderer::commit()
{
  m_bgColor = getParam<vec4>("backgroundColor", vec4(1.f));
  m_pixelSamples = getParam<int>("pixelSamples", 1);
  markUpdated();
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Renderer *);

} // namespace anari
