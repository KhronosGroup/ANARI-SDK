// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"
// subtypes
#include "Image1D.h"
#include "Image2D.h"
#include "Image3D.h"
#include "PrimitiveSampler.h"
#include "TransformSampler.h"

namespace helide_gpu {

Sampler::Sampler(HelideGPUDeviceGlobalState *s) : Object(ANARI_SAMPLER, s) {}

Sampler *Sampler::createInstance(
    std::string_view subtype, HelideGPUDeviceGlobalState *s)
{
  if (subtype == "image1D")
    return new Image1D(s);
  if (subtype == "image2D")
    return new Image2D(s);
  if (subtype == "image3D")
    return new Image3D(s);
  if (subtype == "transform")
    return new TransformSampler(s);
  if (subtype == "primitive")
    return new PrimitiveSampler(s);
  return (Sampler *)new UnknownObject(ANARI_SAMPLER, s);
}

int Sampler::samplerMode() const
{
  return 0;
}

int Sampler::inAttrIndex() const
{
  return m_inAttrIndex;
}

const mat4 &Sampler::inTransform() const
{
  return m_inTransform;
}

const vec4 &Sampler::inOffset() const
{
  return m_inOffset;
}

const mat4 &Sampler::outTransform() const
{
  return m_outTransform;
}

const vec4 &Sampler::outOffset() const
{
  return m_outOffset;
}

void Sampler::readCommonParameters()
{
  m_inAttribute = getParamString("inAttribute", "attribute0");
  m_inAttrIndex = resolveAttrIndex(m_inAttribute);
  m_inTransform = getParam<mat4>("inTransform", mat4(1.f));
  m_inOffset = getParam<vec4>("inOffset", vec4(0.f));
  m_outTransform = getParam<mat4>("outTransform", mat4(1.f));
  m_outOffset = getParam<vec4>("outOffset", vec4(0.f));
}

int Sampler::resolveAttrIndex(const std::string &name) const
{
  if (name == "color")
    return 0;
  if (name == "attribute0")
    return 1;
  if (name == "attribute1")
    return 2;
  if (name == "attribute2")
    return 3;
  if (name == "attribute3")
    return 4;
  return 1; // default to attribute0
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Sampler *);
