// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
#include "array/Array1D.h"
#include "gpu/GPUBuffer.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"
// SDL3
#include <SDL3/SDL_gpu.h>
// std
#include <vector>

namespace helide_gpu {

static constexpr int SPHERE_NUM_ATTR_SLOTS = NUM_ATTR_SLOTS;

struct SphereGPUState
{
  // Static icosphere mesh (uploaded once)
  GPUBuffer icoVertexBuffer;
  GPUBuffer icoIndexBuffer;
  uint32_t icoIndexCount{0};

  // Per-sphere storage buffers
  GPUBuffer positions; // slot 0
  GPUBuffer radii; // slot 1
  GPUBuffer attr[SPHERE_NUM_ATTR_SLOTS]; // slots 2-6

  uint32_t sphereCount{0};
  uint32_t attrMask{0};
  uint32_t attrKind{0};
  uint32_t packedComponents{0};
  float uniformRadius{0.01f};
  vec4 uniformAttr[SPHERE_NUM_ATTR_SLOTS]{};
};

struct Sphere : public Geometry
{
  Sphere(HelideGPUDeviceGlobalState *s);
  ~Sphere() override;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;
  box3 bounds() const override;
  const SphereGPUState &gpuState() const;

  void bindPipelineAndBuffers(SDL_GPURenderPass *pass,
      SDL_GPUCommandBuffer *cmd,
      const SurfaceDrawContext &ctx) override;
  void issueDrawCall(SDL_GPURenderPass *pass,
      SDL_GPUCommandBuffer *cmd,
      const SurfaceDrawContext &ctx) override;
  GeometryAttrInfo attrInfo() const override;

 private:
  void gpu_finalizeGeometry();
  void gpu_freeGeometry();

  helium::ChangeObserverPtr<Array1D> m_positions;
  helium::ChangeObserverPtr<Array1D> m_radii;
  float m_uniformRadius{0.01f};

  // Per-vertex and per-primitive arrays for each attribute slot
  // Slot 0 = color, 1-4 = attribute0..3
  std::vector<helium::ChangeObserverPtr<Array1D>> m_vertexAttr;
  std::vector<helium::ChangeObserverPtr<Array1D>> m_primitiveAttr;

  SphereGPUState m_gpuState;
};

} // namespace helide_gpu
