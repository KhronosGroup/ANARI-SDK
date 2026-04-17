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

struct TriangleGPUState
{
  GPUBuffer positions; // slot 0
  GPUBuffer normals; // slot 1
  GPUBuffer indices; // slot 2
  GPUBuffer attr[NUM_ATTR_SLOTS]; // slots 3-7 (legacy) or 1-5 (mesh path)
  GPUBuffer sourcePrimitiveIds; // source primitive index per generated triangle
  GPUBuffer primitiveIds; // optional primitive.id remap per source primitive

  // Explicit vertex-input representation used by triangle geometry.
  GPUBuffer vertexData;
  GPUBuffer sourceVertexIds;
  uint32_t vertexCount{0};

  uint32_t triangleCount{0};
  uint32_t posCount{0};
  uint32_t attrMask{0}; // bit i = slot i has data
  uint32_t attrKind{0}; // bit i = slot i is per-primitive
  uint32_t packedComponents{0}; // 4 bits per slot
  vec4 uniformAttr[NUM_ATTR_SLOTS]{}; // fallback [color, attr0..3]
  bool hasIndices{false};
  bool hasNormals{false};
  bool hasSourcePrimitiveIds{false};
  bool hasPrimitiveIds{false};
};

struct Triangle : public Geometry
{
  Triangle(HelideGPUDeviceGlobalState *s);
  ~Triangle() override;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;
  box3 bounds() const override;
  const TriangleGPUState &gpuState() const;

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
  helium::ChangeObserverPtr<Array1D> m_normals;
  helium::ChangeObserverPtr<Array1D> m_indices;
  helium::ChangeObserverPtr<Array1D> m_primitiveIds;

  // Per-vertex and per-primitive arrays for each attribute slot
  // Slot 0 = color, 1-4 = attribute0..3
  std::vector<helium::ChangeObserverPtr<Array1D>> m_vertexAttr;
  std::vector<helium::ChangeObserverPtr<Array1D>> m_primitiveAttr;

  TriangleGPUState m_gpuState;
};

} // namespace helide_gpu
