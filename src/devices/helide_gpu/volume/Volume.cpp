// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"
#include "TransferFunction1D.h"

namespace helide_gpu {

Volume::Volume(HelideGPUDeviceGlobalState *s) : Object(ANARI_VOLUME, s) {}

Volume *Volume::createInstance(
    std::string_view subtype, HelideGPUDeviceGlobalState *s)
{
  if (subtype == "transferFunction1D")
    return new TransferFunction1D(s);
  return (Volume *)new UnknownObject(ANARI_VOLUME, s);
}

void Volume::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
}

bool Volume::isValid() const
{
  return false;
}

box3 Volume::bounds() const
{
  return {};
}

SDL_GPUTexture *Volume::fieldTexture() const
{
  return nullptr;
}

SDL_GPUSampler *Volume::fieldSampler() const
{
  return nullptr;
}

SDL_GPUTexture *Volume::tfTexture() const
{
  return nullptr;
}

SDL_GPUSampler *Volume::tfSampler() const
{
  return nullptr;
}

SDL_GPUBuffer *Volume::cubeVertexBuffer() const
{
  return nullptr;
}

vec3 Volume::fieldOrigin() const
{
  return vec3(0.f);
}

vec3 Volume::fieldSpacing() const
{
  return vec3(1.f);
}

uvec3 Volume::fieldDims() const
{
  return uvec3(0u);
}

float Volume::stepSize() const
{
  return 1.f;
}

float Volume::unitDistance() const
{
  return 1.f;
}

vec2 Volume::valueRange() const
{
  return vec2(0.f, 1.f);
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Volume *);
