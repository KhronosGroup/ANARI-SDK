// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// SDL3
#include <SDL3/SDL_gpu.h>

namespace helide_gpu {

static constexpr int NUM_ATTR_SLOTS = 5;

struct SurfaceDrawContext
{
  mat4 MVP;
  mat4 MV;
  mat4 M;
  mat4 P;
  float ambientRadiance;
  float eyeLightBlendRatio;
  uint32_t objectId;
  uint32_t instanceId;
};

struct GeometryAttrInfo
{
  uint32_t attrMask;
  uint32_t attrKind;
  uint32_t packedComponents;
  const vec4 *uniformAttr;
};

struct Geometry : public Object
{
  Geometry(HelideGPUDeviceGlobalState *s);
  ~Geometry() override = default;
  static Geometry *createInstance(
      std::string_view subtype, HelideGPUDeviceGlobalState *s);

  virtual box3 bounds() const;

  virtual void bindPipelineAndBuffers(SDL_GPURenderPass *pass,
      SDL_GPUCommandBuffer *cmd,
      const SurfaceDrawContext &ctx) = 0;
  virtual void issueDrawCall(SDL_GPURenderPass *pass,
      SDL_GPUCommandBuffer *cmd,
      const SurfaceDrawContext &ctx) = 0;
  virtual GeometryAttrInfo attrInfo() const = 0;
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Geometry *, ANARI_GEOMETRY);
