// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
#include "Matte.h"

namespace helide_gpu {

Material::Material(HelideGPUDeviceGlobalState *s) : Object(ANARI_MATERIAL, s) {}

vec3 Material::color() const
{
  return m_color;
}

const std::string &Material::colorAttribute() const
{
  return m_colorAttribute;
}

const Sampler *Material::colorSampler() const
{
  return m_colorSampler.ptr;
}

float Material::opacity() const
{
  return m_opacity;
}

int32_t Material::alphaMode() const
{
  return m_alphaMode;
}

float Material::alphaCutoff() const
{
  return m_alphaCutoff;
}

Material *Material::createInstance(
    std::string_view subtype, HelideGPUDeviceGlobalState *s)
{
  if (subtype == "matte" || subtype == "physicallyBased")
    return new Matte(s);
  return (Material *)new UnknownObject(ANARI_MATERIAL, s);
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Material *);
