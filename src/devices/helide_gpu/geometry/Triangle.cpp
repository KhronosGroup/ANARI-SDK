// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Triangle.h"
// anari
#include <anari/frontend/type_utility.h>
// std
#include <algorithm>
#include <cstring>

namespace helide_gpu {

// Triangle definitions ////////////////////////////////////////////////////////

Triangle::Triangle(HelideGPUDeviceGlobalState *s)
    : Geometry(s),
      m_positions(this),
      m_normals(this),
      m_indices(this)
{
  m_vertexAttr.reserve(NUM_ATTR_SLOTS);
  m_primitiveAttr.reserve(NUM_ATTR_SLOTS);
  for (int i = 0; i < NUM_ATTR_SLOTS; ++i) {
    m_vertexAttr.emplace_back(this);
    m_primitiveAttr.emplace_back(this);
  }
}

Triangle::~Triangle()
{
  gpu_enqueue_method(this, &Triangle::gpu_freeGeometry).wait();
}

void Triangle::commitParameters()
{
  m_positions = getParamObject<Array1D>("vertex.position");
  m_normals = getParamObject<Array1D>("vertex.normal");
  m_indices = getParamObject<Array1D>("primitive.index");

  // Attribute slot 0 = color, slots 1-4 = attribute0..3
  m_vertexAttr[0] = getParamObject<Array1D>("vertex.color");
  m_vertexAttr[1] = getParamObject<Array1D>("vertex.attribute0");
  m_vertexAttr[2] = getParamObject<Array1D>("vertex.attribute1");
  m_vertexAttr[3] = getParamObject<Array1D>("vertex.attribute2");
  m_vertexAttr[4] = getParamObject<Array1D>("vertex.attribute3");

  m_primitiveAttr[0] = getParamObject<Array1D>("primitive.color");
  m_primitiveAttr[1] = getParamObject<Array1D>("primitive.attribute0");
  m_primitiveAttr[2] = getParamObject<Array1D>("primitive.attribute1");
  m_primitiveAttr[3] = getParamObject<Array1D>("primitive.attribute2");
  m_primitiveAttr[4] = getParamObject<Array1D>("primitive.attribute3");

  // Uniform fallbacks (color defaults to white, attributes to {0,0,0,1})
  m_gpuState.uniformAttr[0] = getParam<vec4>("color", vec4(1.f));
  m_gpuState.uniformAttr[1] =
      getParam<vec4>("attribute0", vec4(0.f, 0.f, 0.f, 1.f));
  m_gpuState.uniformAttr[2] =
      getParam<vec4>("attribute1", vec4(0.f, 0.f, 0.f, 1.f));
  m_gpuState.uniformAttr[3] =
      getParam<vec4>("attribute2", vec4(0.f, 0.f, 0.f, 1.f));
  m_gpuState.uniformAttr[4] =
      getParam<vec4>("attribute3", vec4(0.f, 0.f, 0.f, 1.f));
}

void Triangle::finalize()
{
  if (!m_positions) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on triangle geometry");
  }

  gpu_enqueue_method(this, &Triangle::gpu_finalizeGeometry);
}

box3 Triangle::bounds() const
{
  box3 b;
  if (m_positions) {
    const auto *pos = m_positions->beginAs<vec3>();
    for (size_t i = 0, n = m_positions->size(); i < n; ++i)
      b.extend(pos[i]);
  }
  return b;
}

bool Triangle::isValid() const
{
  return m_gpuState.vertexData.valid() || m_gpuState.positions.valid();
}

const TriangleGPUState &Triangle::gpuState() const
{
  return m_gpuState;
}

void Triangle::bindPipelineAndBuffers(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const SurfaceDrawContext &ctx)
{
  auto &gs = m_gpuState;
  auto *ds = deviceState();
  auto *dummyBuf = ds->gpu.dummyStorageBuffer;

  if (ds->gpu.triangleMeshPipeline && gs.vertexData) {
    SDL_BindGPUGraphicsPipeline(pass, ds->gpu.triangleMeshPipeline);

    SDL_GPUBufferBinding vb{};
    vb.buffer = gs.vertexData.sdlBuffer();
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

    const bool usingMSL =
        ds->gpu.sdlDevice.shaderFormat == SDL_GPU_SHADERFORMAT_MSL;
    SDL_GPUBuffer *storageBufs[7];
    if (usingMSL) {
      storageBufs[0] =
          gs.sourcePrimitiveIds ? gs.sourcePrimitiveIds.sdlBuffer() : dummyBuf;
      storageBufs[1] = gs.sourceVertexIds ? gs.sourceVertexIds.sdlBuffer()
                                          : dummyBuf;
      storageBufs[2] = gs.attr[0] ? gs.attr[0].sdlBuffer() : dummyBuf;
      storageBufs[3] = gs.attr[1] ? gs.attr[1].sdlBuffer() : dummyBuf;
      storageBufs[4] = gs.attr[2] ? gs.attr[2].sdlBuffer() : dummyBuf;
      storageBufs[5] = gs.attr[3] ? gs.attr[3].sdlBuffer() : dummyBuf;
      storageBufs[6] = gs.attr[4] ? gs.attr[4].sdlBuffer() : dummyBuf;
    } else {
      storageBufs[0] = gs.sourceVertexIds ? gs.sourceVertexIds.sdlBuffer()
                                          : dummyBuf;
      storageBufs[1] =
          gs.sourcePrimitiveIds ? gs.sourcePrimitiveIds.sdlBuffer() : dummyBuf;
      storageBufs[2] = gs.attr[0] ? gs.attr[0].sdlBuffer() : dummyBuf;
      storageBufs[3] = gs.attr[1] ? gs.attr[1].sdlBuffer() : dummyBuf;
      storageBufs[4] = gs.attr[2] ? gs.attr[2].sdlBuffer() : dummyBuf;
      storageBufs[5] = gs.attr[3] ? gs.attr[3].sdlBuffer() : dummyBuf;
      storageBufs[6] = gs.attr[4] ? gs.attr[4].sdlBuffer() : dummyBuf;
    }
    SDL_BindGPUVertexStorageBuffers(pass, 0, storageBufs, 7);

    struct
    {
      mat4 mvp;
      mat4 mv;
      mat4 m;
      uvec4 geomInfo;
      uvec4 attrInfo;
    } u{ctx.MVP,
        ctx.MV,
        ctx.M,
        uvec4(gs.attrMask, gs.hasSourcePrimitiveIds ? 1u : 0u, 0u, 0u),
        uvec4(gs.attrKind, gs.packedComponents, 0u, 0u)};
    SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));
  } else if (ds->gpu.trianglePipeline && gs.positions) {
    SDL_BindGPUGraphicsPipeline(pass, ds->gpu.trianglePipeline);

    SDL_GPUBufferBinding dummyVB{};
    dummyVB.buffer = ds->gpu.dummyVertexBuffer;
    SDL_BindGPUVertexBuffers(pass, 0, &dummyVB, 1);

    const bool usingMSL =
        ds->gpu.sdlDevice.shaderFormat == SDL_GPU_SHADERFORMAT_MSL;
    SDL_GPUBuffer *storageBufs[9];
    if (usingMSL) {
      storageBufs[0] = gs.positions.sdlBuffer();
      storageBufs[1] = gs.normals ? gs.normals.sdlBuffer() : dummyBuf;
      storageBufs[2] =
          gs.sourcePrimitiveIds ? gs.sourcePrimitiveIds.sdlBuffer() : dummyBuf;
      storageBufs[3] = gs.indices ? gs.indices.sdlBuffer() : dummyBuf;
      storageBufs[4] = gs.attr[0] ? gs.attr[0].sdlBuffer() : dummyBuf;
      storageBufs[5] = gs.attr[1] ? gs.attr[1].sdlBuffer() : dummyBuf;
      storageBufs[6] = gs.attr[2] ? gs.attr[2].sdlBuffer() : dummyBuf;
      storageBufs[7] = gs.attr[3] ? gs.attr[3].sdlBuffer() : dummyBuf;
      storageBufs[8] = gs.attr[4] ? gs.attr[4].sdlBuffer() : dummyBuf;
    } else {
      storageBufs[0] = gs.positions.sdlBuffer();
      storageBufs[1] = gs.normals ? gs.normals.sdlBuffer() : dummyBuf;
      storageBufs[2] = gs.indices ? gs.indices.sdlBuffer() : dummyBuf;
      storageBufs[3] = gs.attr[0] ? gs.attr[0].sdlBuffer() : dummyBuf;
      storageBufs[4] = gs.attr[1] ? gs.attr[1].sdlBuffer() : dummyBuf;
      storageBufs[5] = gs.attr[2] ? gs.attr[2].sdlBuffer() : dummyBuf;
      storageBufs[6] = gs.attr[3] ? gs.attr[3].sdlBuffer() : dummyBuf;
      storageBufs[7] = gs.attr[4] ? gs.attr[4].sdlBuffer() : dummyBuf;
      storageBufs[8] =
          gs.sourcePrimitiveIds ? gs.sourcePrimitiveIds.sdlBuffer() : dummyBuf;
    }
    SDL_BindGPUVertexStorageBuffers(pass, 0, storageBufs, 9);

    struct
    {
      mat4 mvp;
      mat4 mv;
      mat4 m;
      uvec4 geomInfo;
      uvec4 attrInfo;
    } u{ctx.MVP,
        ctx.MV,
        ctx.M,
        uvec4(gs.posCount,
            gs.hasIndices ? 1u : 0u,
            gs.hasNormals ? 1u : 0u,
            gs.attrMask),
        uvec4(gs.attrKind,
            gs.packedComponents,
            gs.hasSourcePrimitiveIds ? 1u : 0u,
            0u)};
    SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));
  }
}

void Triangle::issueDrawCall(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const SurfaceDrawContext &ctx)
{
  auto &gs = m_gpuState;
  auto *ds = deviceState();

  if (ds->gpu.triangleMeshPipeline && gs.vertexData) {
    SDL_DrawGPUPrimitives(pass, gs.vertexCount, 1, 0, 0);
  } else if (ds->gpu.trianglePipeline && gs.positions) {
    SDL_DrawGPUPrimitives(pass, gs.triangleCount * 3, 1, 0, 0);
  }
}

GeometryAttrInfo Triangle::attrInfo() const
{
  return {m_gpuState.attrMask,
      m_gpuState.attrKind,
      m_gpuState.packedComponents,
      m_gpuState.uniformAttr};
}

void Triangle::gpu_finalizeGeometry()
{
  if (!m_positions) {
    gpu_freeGeometry();
    return;
  }

  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  const size_t numVerts = m_positions->size();
  const size_t numPrims = m_indices ? m_indices->size() : numVerts / 3;

  // Helper: upload array to buffer, resetting buffer if array is absent.
  auto syncBuffer =
      [dev](GPUBuffer &buf, const Array1D *arr, bool convertToFloat = false) {
        if (!arr || arr->size() == 0) {
          buf = {};
          return;
        }
        if (!buf)
          buf = GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
        buf.uploadArray(arr, convertToFloat);
      };

  // Build an explicit draw-ready vertex stream for triangle geometry.
  // This avoids the gl_VertexIndex + SSBO fetch path on Metal.
  std::vector<float> vertexData(numPrims * 3 * 6, 0.f);
  std::vector<uint32_t> sourceVertexIds(numPrims * 3, 0u);

  const auto *pos = m_positions->beginAs<vec3>();
  const auto *norm =
      (m_normals && m_normals->size() >= numVerts) ? m_normals->beginAs<vec3>()
                                                   : nullptr;

  auto safeNormalize = [](const vec3 &v) {
    const float len2 = dot(v, v);
    return len2 > 1e-20f ? v * inversesqrt(len2) : vec3(0.f, 0.f, 1.f);
  };

  auto getIndex = [&](size_t i) {
    if (!m_indices)
      return uvec3(3u * i, 3u * i + 1u, 3u * i + 2u);

    if (m_indices->elementType() == ANARI_UINT32_VEC3)
      return m_indices->beginAs<uvec3>()[i];

    const auto *idx64 = static_cast<const uint64_t *>(m_indices->begin());
    const size_t offset = 3 * i;
    return uvec3(idx64[offset + 0], idx64[offset + 1], idx64[offset + 2]);
  };

  for (size_t i = 0; i < numPrims; ++i) {
    const uvec3 tri = getIndex(i);
    const vec3 p0 = pos[tri.x];
    const vec3 p1 = pos[tri.y];
    const vec3 p2 = pos[tri.z];
    const vec3 faceNormal = safeNormalize(cross(p1 - p0, p2 - p0));
    const uint32_t ids[3] = {tri.x, tri.y, tri.z};

    for (int c = 0; c < 3; ++c) {
      const uint32_t srcId = ids[c];
      const vec3 p = pos[srcId];
      const vec3 n = norm ? norm[srcId] : faceNormal;
      const size_t base = (i * 3 + c) * 6;
      vertexData[base + 0] = p.x;
      vertexData[base + 1] = p.y;
      vertexData[base + 2] = p.z;
      vertexData[base + 3] = n.x;
      vertexData[base + 4] = n.y;
      vertexData[base + 5] = n.z;
      sourceVertexIds[i * 3 + c] = srcId;
    }
  }

  if (!m_gpuState.vertexData)
    m_gpuState.vertexData = GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_VERTEX);
  m_gpuState.vertexData.upload(vertexData.data(), vertexData.size());

  if (!m_gpuState.sourceVertexIds)
    m_gpuState.sourceVertexIds =
        GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
  m_gpuState.sourceVertexIds.upload(sourceVertexIds.data(), sourceVertexIds.size());

  // Legacy buffers are unused for direct triangle rendering.
  m_gpuState.positions = {};
  m_gpuState.normals = {};
  m_gpuState.indices = {};
  m_gpuState.posCount = static_cast<uint32_t>(numVerts);
  m_gpuState.vertexCount = static_cast<uint32_t>(numPrims * 3);
  m_gpuState.triangleCount = static_cast<uint32_t>(numPrims);
  m_gpuState.hasIndices = false;
  m_gpuState.hasNormals = true;
  m_gpuState.sourcePrimitiveIds = {};
  m_gpuState.hasSourcePrimitiveIds = false;

  // --- Sync attribute arrays ---
  m_gpuState.attrMask = 0;
  m_gpuState.attrKind = 0;
  m_gpuState.packedComponents = 0;

  for (int s = 0; s < NUM_ATTR_SLOTS; ++s) {
    const auto *vtxArr = m_vertexAttr[s].get();
    const auto *primArr = m_primitiveAttr[s].get();

    if (vtxArr && vtxArr->size() >= numVerts) {
      syncBuffer(m_gpuState.attr[s], vtxArr, true);
      if (m_gpuState.attr[s].valid()) {
        m_gpuState.attrMask |= (1u << s);
        m_gpuState.packedComponents |=
            (anari::componentsOf(vtxArr->elementType()) << (s * 4));
      }
    } else if (primArr && primArr->size() >= numPrims) {
      syncBuffer(m_gpuState.attr[s], primArr, true);
      if (m_gpuState.attr[s].valid()) {
        m_gpuState.attrMask |= (1u << s);
        m_gpuState.attrKind |= (1u << s);
        m_gpuState.packedComponents |=
            (anari::componentsOf(primArr->elementType()) << (s * 4));
      }
    } else {
      m_gpuState.attr[s] = {};
    }
  }
}

void Triangle::gpu_freeGeometry()
{
  m_gpuState.positions = {};
  m_gpuState.normals = {};
  m_gpuState.indices = {};
  m_gpuState.vertexData = {};
  m_gpuState.sourceVertexIds = {};
  for (int s = 0; s < NUM_ATTR_SLOTS; ++s)
    m_gpuState.attr[s] = {};
  m_gpuState.sourcePrimitiveIds = {};
  m_gpuState.vertexCount = 0;
  m_gpuState.triangleCount = 0;
  m_gpuState.posCount = 0;
  m_gpuState.attrMask = 0;
  m_gpuState.attrKind = 0;
  m_gpuState.packedComponents = 0;
  m_gpuState.hasIndices = false;
  m_gpuState.hasNormals = false;
  m_gpuState.hasSourcePrimitiveIds = false;
}

} // namespace helide_gpu
