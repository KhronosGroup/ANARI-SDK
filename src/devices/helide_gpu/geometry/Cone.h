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
#include <string>
#include <vector>

namespace helide_gpu {

struct ConeGPUState
{
  GPUBuffer vertexData;
  GPUBuffer sourceVertexIds;
  GPUBuffer sourcePrimitiveIds;
  GPUBuffer attr[NUM_ATTR_SLOTS];

  uint32_t vertexCount{0};
  uint32_t triangleCount{0};
  uint32_t attrMask{0};
  uint32_t attrKind{0};
  uint32_t packedComponents{0};
  vec4 uniformAttr[NUM_ATTR_SLOTS]{};
  bool hasSourcePrimitiveIds{false};
};

struct Cone : public Geometry
{
  Cone(HelideGPUDeviceGlobalState *s);
  ~Cone() override;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;
  box3 bounds() const override;
  const ConeGPUState &gpuState() const;

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
  helium::ChangeObserverPtr<Array1D> m_indices;
  helium::ChangeObserverPtr<Array1D> m_radii;
  helium::ChangeObserverPtr<Array1D> m_caps;
  float m_uniformRadius{1.f};
  std::string m_globalCaps{"none"};

  std::vector<helium::ChangeObserverPtr<Array1D>> m_vertexAttr;
  std::vector<helium::ChangeObserverPtr<Array1D>> m_primitiveAttr;

  ConeGPUState m_gpuState;
};

} // namespace helide_gpu
