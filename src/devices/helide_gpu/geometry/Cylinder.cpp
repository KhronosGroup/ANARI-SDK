// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Cylinder.h"
// anari
#include <anari/frontend/type_utility.h>
// std
#include <algorithm>
#include <cmath>

namespace helide_gpu {

namespace {

static constexpr uint32_t kCylinderSides = 16;

uvec2 readIndex(const Array1D *indexArray, size_t i)
{
  if (!indexArray)
    return uvec2(2u * i, 2u * i + 1u);

  if (indexArray->elementType() == ANARI_UINT32_VEC2)
    return indexArray->beginAs<uvec2>()[i];

  const auto *idx64 = static_cast<const uint64_t *>(indexArray->begin());
  const size_t offset = 2 * i;
  return uvec2(idx64[offset + 0], idx64[offset + 1]);
}

bool readCapValue(const Array1D *caps, size_t i)
{
  if (!caps || i >= caps->size())
    return false;

  if (caps->elementType() == ANARI_UINT8)
    return caps->beginAs<uint8_t>()[i] != 0u;

  return caps->readAsAttributeValue(static_cast<int32_t>(i)).x != 0.f;
}

void resolveGlobalCaps(
    const std::string &caps, bool &firstCapEnabled, bool &secondCapEnabled)
{
  firstCapEnabled = caps == "first" || caps == "both";
  secondCapEnabled = caps == "second" || caps == "both";
}

vec3 safeNormalize(const vec3 &v, const vec3 &fallback)
{
  const float len2 = dot(v, v);
  return len2 > 1e-20f ? v * inversesqrt(len2) : fallback;
}

} // namespace

Cylinder::Cylinder(HelideGPUDeviceGlobalState *s)
    : Geometry(s),
      m_positions(this),
      m_indices(this),
      m_radii(this),
      m_caps(this)
{
  m_vertexAttr.reserve(NUM_ATTR_SLOTS);
  m_primitiveAttr.reserve(NUM_ATTR_SLOTS);
  for (int i = 0; i < NUM_ATTR_SLOTS; ++i) {
    m_vertexAttr.emplace_back(this);
    m_primitiveAttr.emplace_back(this);
  }
}

Cylinder::~Cylinder()
{
  gpu_enqueue_method(this, &Cylinder::gpu_freeGeometry).wait();
}

void Cylinder::commitParameters()
{
  m_positions = getParamObject<Array1D>("vertex.position");
  m_indices = getParamObject<Array1D>("primitive.index");
  m_radii = getParamObject<Array1D>("primitive.radius");
  m_caps = getParamObject<Array1D>("vertex.cap");
  m_uniformRadius = getParam<float>("radius", 0.01f);
  m_globalCaps = getParamString("caps", "none");

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

void Cylinder::finalize()
{
  if (!m_positions) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on cylinder geometry");
  }

  gpu_enqueue_method(this, &Cylinder::gpu_finalizeGeometry);
}

box3 Cylinder::bounds() const
{
  box3 b;
  if (!m_positions)
    return b;

  const auto *pos = m_positions->beginAs<vec3>();
  const size_t numVerts = m_positions->size();
  const size_t numPrims = m_indices ? m_indices->size() : numVerts / 2;
  const bool hasPerPrimRadius = m_radii && m_radii->size() >= numPrims;
  const float *radii = hasPerPrimRadius ? m_radii->beginAs<float>() : nullptr;

  for (size_t i = 0; i < numPrims; ++i) {
    const uvec2 idx = readIndex(m_indices.get(), i);
    if (idx.x >= numVerts || idx.y >= numVerts)
      continue;

    const float radius = radii ? radii[i] : m_uniformRadius;
    if (radius <= 0.f)
      continue;

    b.extend(pos[idx.x] - vec3(radius));
    b.extend(pos[idx.x] + vec3(radius));
    b.extend(pos[idx.y] - vec3(radius));
    b.extend(pos[idx.y] + vec3(radius));
  }

  return b;
}

bool Cylinder::isValid() const
{
  return m_gpuState.vertexData.valid();
}

const CylinderGPUState &Cylinder::gpuState() const
{
  return m_gpuState;
}

void Cylinder::bindPipelineAndBuffers(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const SurfaceDrawContext &ctx)
{
  auto &gs = m_gpuState;
  auto *ds = deviceState();
  auto *dummyBuf = ds->gpu.dummyStorageBuffer;

  if (!ds->gpu.triangleMeshPipeline || !gs.vertexData)
    return;

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
    storageBufs[1] =
        gs.sourceVertexIds ? gs.sourceVertexIds.sdlBuffer() : dummyBuf;
    storageBufs[2] = gs.attr[0] ? gs.attr[0].sdlBuffer() : dummyBuf;
    storageBufs[3] = gs.attr[1] ? gs.attr[1].sdlBuffer() : dummyBuf;
    storageBufs[4] = gs.attr[2] ? gs.attr[2].sdlBuffer() : dummyBuf;
    storageBufs[5] = gs.attr[3] ? gs.attr[3].sdlBuffer() : dummyBuf;
    storageBufs[6] = gs.attr[4] ? gs.attr[4].sdlBuffer() : dummyBuf;
  } else {
    storageBufs[0] =
        gs.sourceVertexIds ? gs.sourceVertexIds.sdlBuffer() : dummyBuf;
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
}

void Cylinder::issueDrawCall(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const SurfaceDrawContext &ctx)
{
  auto &gs = m_gpuState;
  auto *ds = deviceState();

  if (!ds->gpu.triangleMeshPipeline || !gs.vertexData)
    return;

  SDL_DrawGPUPrimitives(pass, gs.vertexCount, 1, 0, 0);
}

GeometryAttrInfo Cylinder::attrInfo() const
{
  return {m_gpuState.attrMask,
      m_gpuState.attrKind,
      m_gpuState.packedComponents,
      m_gpuState.uniformAttr};
}

void Cylinder::gpu_finalizeGeometry()
{
  if (!m_positions) {
    gpu_freeGeometry();
    return;
  }

  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  const size_t numVerts = m_positions->size();
  const size_t numPrims = m_indices ? m_indices->size() : numVerts / 2;
  const auto *pos = m_positions->beginAs<vec3>();
  const bool hasPerPrimRadius = m_radii && m_radii->size() >= numPrims;
  const float *radii = hasPerPrimRadius ? m_radii->beginAs<float>() : nullptr;
  const size_t capCount = m_caps ? m_caps->size() : 0;

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

  std::vector<float> vertexData;
  std::vector<uint32_t> sourceVertexIds;
  std::vector<uint32_t> sourcePrimitiveIds;
  vertexData.reserve(numPrims * kCylinderSides * 24 * 6);
  sourceVertexIds.reserve(numPrims * kCylinderSides * 24);
  sourcePrimitiveIds.reserve(numPrims * kCylinderSides * 4);

  auto appendVertex = [&](const vec3 &p, const vec3 &n, uint32_t sourceVertexId) {
    vertexData.push_back(p.x);
    vertexData.push_back(p.y);
    vertexData.push_back(p.z);
    vertexData.push_back(n.x);
    vertexData.push_back(n.y);
    vertexData.push_back(n.z);
    sourceVertexIds.push_back(sourceVertexId);
  };

  auto appendTriangle = [&](const vec3 &p0,
                            const vec3 &n0,
                            uint32_t src0,
                            const vec3 &p1,
                            const vec3 &n1,
                            uint32_t src1,
                            const vec3 &p2,
                            const vec3 &n2,
                            uint32_t src2,
                            uint32_t primIdx) {
    appendVertex(p0, n0, src0);
    appendVertex(p1, n1, src1);
    appendVertex(p2, n2, src2);
    sourcePrimitiveIds.push_back(primIdx);
  };

  for (size_t i = 0; i < numPrims; ++i) {
    const uvec2 idx = readIndex(m_indices.get(), i);
    if (idx.x >= numVerts || idx.y >= numVerts)
      continue;

    const float radius = radii ? radii[i] : m_uniformRadius;
    if (radius <= 0.f)
      continue;

    const vec3 p0 = pos[idx.x];
    const vec3 p1 = pos[idx.y];
    const vec3 axis = p1 - p0;
    const float axisLen2 = dot(axis, axis);
    if (axisLen2 <= 1e-20f)
      continue;

    const vec3 dir = axis * inversesqrt(axisLen2);
    const vec3 ref = glm::abs(dir.z) < 0.999f ? vec3(0.f, 0.f, 1.f)
                                               : vec3(0.f, 1.f, 0.f);
    const vec3 tangent = safeNormalize(cross(ref, dir), vec3(1.f, 0.f, 0.f));
    const vec3 bitangent = safeNormalize(cross(dir, tangent), vec3(0.f, 1.f, 0.f));

    bool firstCapEnabled = false;
    bool secondCapEnabled = false;
    resolveGlobalCaps(m_globalCaps, firstCapEnabled, secondCapEnabled);

    if (m_caps && capCount >= numVerts) {
      firstCapEnabled = readCapValue(m_caps.get(), idx.x);
      secondCapEnabled = readCapValue(m_caps.get(), idx.y);
    } else if (m_caps && capCount >= numPrims * 2) {
      firstCapEnabled = readCapValue(m_caps.get(), 2 * i);
      secondCapEnabled = readCapValue(m_caps.get(), 2 * i + 1);
    } else if (m_caps && capCount >= numPrims) {
      const bool capEnabled = readCapValue(m_caps.get(), i);
      firstCapEnabled = capEnabled;
      secondCapEnabled = capEnabled;
    }

    const float angleStep = glm::two_pi<float>() / float(kCylinderSides);
    for (uint32_t side = 0; side < kCylinderSides; ++side) {
      const float a0 = angleStep * float(side);
      const float a1 = angleStep * float((side + 1) % kCylinderSides);
      const vec3 r0 = tangent * std::cos(a0) + bitangent * std::sin(a0);
      const vec3 r1 = tangent * std::cos(a1) + bitangent * std::sin(a1);

      const vec3 p00 = p0 + r0 * radius;
      const vec3 p01 = p0 + r1 * radius;
      const vec3 p10 = p1 + r0 * radius;
      const vec3 p11 = p1 + r1 * radius;

      appendTriangle(
          p00, r0, idx.x, p10, r0, idx.y, p11, r1, idx.y, static_cast<uint32_t>(i));
      appendTriangle(
          p00, r0, idx.x, p11, r1, idx.y, p01, r1, idx.x, static_cast<uint32_t>(i));

      if (firstCapEnabled) {
        const vec3 capNormal = -dir;
        appendTriangle(p0,
            capNormal,
            idx.x,
            p01,
            capNormal,
            idx.x,
            p00,
            capNormal,
            idx.x,
            static_cast<uint32_t>(i));
      }

      if (secondCapEnabled) {
        const vec3 capNormal = dir;
        appendTriangle(p1,
            capNormal,
            idx.y,
            p10,
            capNormal,
            idx.y,
            p11,
            capNormal,
            idx.y,
            static_cast<uint32_t>(i));
      }
    }
  }

  if (sourcePrimitiveIds.empty()) {
    gpu_freeGeometry();
    return;
  }

  if (!m_gpuState.vertexData)
    m_gpuState.vertexData = GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_VERTEX);
  m_gpuState.vertexData.upload(vertexData.data(), vertexData.size());

  if (!m_gpuState.sourceVertexIds)
    m_gpuState.sourceVertexIds =
        GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
  m_gpuState.sourceVertexIds.upload(sourceVertexIds.data(), sourceVertexIds.size());

  if (!m_gpuState.sourcePrimitiveIds)
    m_gpuState.sourcePrimitiveIds =
        GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
  m_gpuState.sourcePrimitiveIds.upload(
      sourcePrimitiveIds.data(), sourcePrimitiveIds.size());

  m_gpuState.vertexCount = static_cast<uint32_t>(sourceVertexIds.size());
  m_gpuState.triangleCount = static_cast<uint32_t>(sourcePrimitiveIds.size());
  m_gpuState.hasSourcePrimitiveIds = true;

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

void Cylinder::gpu_freeGeometry()
{
  m_gpuState.vertexData = {};
  m_gpuState.sourceVertexIds = {};
  m_gpuState.sourcePrimitiveIds = {};
  for (int s = 0; s < NUM_ATTR_SLOTS; ++s)
    m_gpuState.attr[s] = {};
  m_gpuState.vertexCount = 0;
  m_gpuState.triangleCount = 0;
  m_gpuState.attrMask = 0;
  m_gpuState.attrKind = 0;
  m_gpuState.packedComponents = 0;
  m_gpuState.hasSourcePrimitiveIds = false;
}

} // namespace helide_gpu
