// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene/World.h"
// std
#include <limits>

namespace anari {
namespace example_device {

typedef struct
{
  const char *desc;
  bool req;
  union Def
  {
    int i;
    vec4 v;
    Def(int i);
    Def(vec4 v);
  } def;
} ParameterInfo;

struct RenderedSample
{
  vec4 color{0.f, 0.f, 0.f, 1.f};
  float depth{std::numeric_limits<float>::infinity()};

  RenderedSample() = default;
  RenderedSample(vec4 c) : color(c) {}
  RenderedSample(vec4 c, float d) : color(c), depth(d) {}
};

struct Renderer : public Object
{
  Renderer() = default;

  virtual void commit() override;

  virtual RenderedSample renderSample(Ray ray, const World &world) const = 0;

  static Renderer *createInstance(const char *type);
  static ANARIParameter *parameters(const char *type);
  static FactoryMapPtr<Renderer> g_renderers;
  static void init();
  static ANARIParameter g_parameters[];
  static ParameterInfo g_parameterinfos[];

  int pixelSamples() const;

 protected:
  vec4 m_bgColor;
  int m_pixelSamples{1};
};

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::Renderer *, ANARI_RENDERER);

} // namespace anari
