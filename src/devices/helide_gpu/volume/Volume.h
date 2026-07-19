// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// SDL3
#include <SDL3/SDL_gpu.h>

namespace helide_gpu {

struct VolumeDrawContext
{
  mat4 M;  // instance transform (not yet combined with fieldToWorld)
  mat4 V;
  mat4 P;
  vec3 camWorldPos;
  uint32_t objectId;
  uint32_t instanceId;
  float jitterSeed;
};

struct Volume : public Object
{
  Volume(HelideGPUDeviceGlobalState *d);
  virtual ~Volume() = default;
  static Volume *createInstance(
      std::string_view subtype, HelideGPUDeviceGlobalState *d);

  void commitParameters() override;

  uint32_t id() const;

  virtual bool isValid() const override;
  virtual box3 bounds() const;

  virtual void draw(SDL_GPURenderPass *pass,
      SDL_GPUCommandBuffer *cmd,
      const VolumeDrawContext &ctx) = 0;

  // GPU resource accessors for rendering
  virtual SDL_GPUTexture *fieldTexture() const;
  virtual SDL_GPUSampler *fieldSampler() const;
  virtual SDL_GPUTexture *tfTexture() const;
  virtual SDL_GPUSampler *tfSampler() const;
  virtual SDL_GPUBuffer *cubeVertexBuffer() const;

  // Field parameters needed for transform computation
  virtual vec3 fieldOrigin() const;
  virtual vec3 fieldSpacing() const;
  virtual uvec3 fieldDims() const;
  virtual float stepSize() const;
  virtual float unitDistance() const;
  virtual vec2 valueRange() const;

 private:
  uint32_t m_id{~0u};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline uint32_t Volume::id() const
{
  return m_id;
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Volume *, ANARI_VOLUME);
