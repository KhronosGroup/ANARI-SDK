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

const Sampler *Material::opacitySampler() const
{
  return m_opacitySampler.ptr;
}

int32_t Material::alphaMode() const
{
  return m_alphaMode;
}

float Material::alphaCutoff() const
{
  return m_alphaCutoff;
}

float Material::metallic() const
{
  return m_metallic;
}

float Material::roughness() const
{
  return m_roughness;
}

const std::string &Material::metallicAttribute() const
{
  return m_metallicAttribute;
}

const std::string &Material::roughnessAttribute() const
{
  return m_roughnessAttribute;
}

const Sampler *Material::metallicSampler() const
{
  return m_metallicSampler.ptr;
}

const Sampler *Material::roughnessSampler() const
{
  return m_roughnessSampler.ptr;
}

float Material::ior() const
{
  return m_ior;
}

vec3 Material::emissive() const
{
  return m_emissive;
}

const Sampler *Material::emissiveSampler() const
{
  return m_emissiveSampler.ptr;
}

const Sampler *Material::normalSampler() const
{
  return m_normalSampler.ptr;
}

Material *Material::createInstance(
    std::string_view subtype, HelideGPUDeviceGlobalState *s)
{
  if (subtype == "matte")
    return new Matte(s, false);
  else if (subtype == "physicallyBased")
    return new Matte(s, true);
  return (Material *)new UnknownObject(ANARI_MATERIAL, s);
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Material *);
