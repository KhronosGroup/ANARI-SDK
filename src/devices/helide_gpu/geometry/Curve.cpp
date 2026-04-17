// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Curve.h"
// anari
#include <anari/frontend/type_utility.h>
// std
#include <algorithm>
#include <cmath>

namespace helide_gpu {

namespace {

static constexpr uint32_t kCurveSides = 16;
static constexpr uint32_t kCapBands = 4;
static constexpr float kRadiusEpsilon = 1e-6f;

uint32_t readIndex(const Array1D *indexArray, size_t i)
{
  if (!indexArray)
    return static_cast<uint32_t>(2u * i);

  if (indexArray->elementType() == ANARI_UINT32)
    return indexArray->beginAs<uint32_t>()[i];

  return static_cast<uint32_t>(indexArray->beginAs<uint64_t>()[i]);
}

bool readPrimitiveIdBuffer(
    GPUBuffer &buf, SDL_GPUDevice *dev, const Array1D *arr, size_t numPrims)
{
  if (!arr || arr->size() < numPrims) {
    buf = {};
    return false;
  }

  std::vector<uint32_t> ids(numPrims, 0u);
  if (arr->elementType() == ANARI_UINT32) {
    const auto *src = arr->beginAs<uint32_t>();
    std::copy(src, src + numPrims, ids.begin());
  } else if (arr->elementType() == ANARI_UINT64) {
    const auto *src = arr->beginAs<uint64_t>();
    for (size_t i = 0; i < numPrims; ++i)
      ids[i] = static_cast<uint32_t>(src[i]);
  } else {
    buf = {};
    return false;
  }

  if (!buf)
    buf = GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
  buf.upload(ids.data(), ids.size());
  return true;
}

vec3 safeNormalize(const vec3 &v, const vec3 &fallback)
{
  const float len2 = dot(v, v);
  return len2 > 1e-20f ? v * inversesqrt(len2) : fallback;
}

vec3 sideNormal(
    const vec3 &radial, const vec3 &dir, float axisLen, float deltaR)
{
  return safeNormalize(axisLen * radial - deltaR * dir, radial);
}

bool isFiniteVec3(const vec3 &v)
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool isFiniteFloat(float v)
{
  return std::isfinite(v);
}

} // namespace

Curve::Curve(HelideGPUDeviceGlobalState *s)
    : Geometry(s),
      m_positions(this),
      m_indices(this),
      m_radii(this),
      m_primitiveIds(this)
{
  m_vertexAttr.reserve(NUM_ATTR_SLOTS);
  m_primitiveAttr.reserve(NUM_ATTR_SLOTS);
  for (int i = 0; i < NUM_ATTR_SLOTS; ++i) {
    m_vertexAttr.emplace_back(this);
    m_primitiveAttr.emplace_back(this);
  }
}

Curve::~Curve()
{
  gpu_enqueue_method(this, &Curve::gpu_freeGeometry).wait();
}

void Curve::commitParameters()
{
  m_positions = getParamObject<Array1D>("vertex.position");
  m_indices = getParamObject<Array1D>("primitive.index");
  m_radii = getParamObject<Array1D>("vertex.radius");
  m_primitiveIds = getParamObject<Array1D>("primitive.id");
  m_uniformRadius = getParam<float>("radius", 1.f);

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

void Curve::finalize()
{
  if (!m_positions) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on curve geometry");
  }

  gpu_enqueue_method(this, &Curve::gpu_finalizeGeometry);
}

box3 Curve::bounds() const
{
  box3 b;
  if (!m_positions)
    return b;

  const auto *pos = m_positions->beginAs<vec3>();
  const size_t numVerts = m_positions->size();
  const size_t numPrims = m_indices ? m_indices->size() : numVerts / 2;
  const bool hasPerVertexRadius = m_radii && m_radii->size() >= numVerts;
  const float *radii = hasPerVertexRadius ? m_radii->beginAs<float>() : nullptr;

  for (size_t i = 0; i < numPrims; ++i) {
    const uint32_t idx0 = readIndex(m_indices.get(), i);
    const uint32_t idx1 = idx0 + 1u;
    if (idx0 >= numVerts || idx1 >= numVerts)
      continue;

    const float r0 = std::max(0.f, radii ? radii[idx0] : m_uniformRadius);
    const float r1 = std::max(0.f, radii ? radii[idx1] : m_uniformRadius);
    if (r0 <= 0.f && r1 <= 0.f)
      continue;

    b.extend(pos[idx0] - vec3(r0));
    b.extend(pos[idx0] + vec3(r0));
    b.extend(pos[idx1] - vec3(r1));
    b.extend(pos[idx1] + vec3(r1));
  }

  return b;
}

bool Curve::isValid() const
{
  return m_gpuState.vertexData.valid();
}

const CurveGPUState &Curve::gpuState() const
{
  return m_gpuState;
}

void Curve::bindPipelineAndBuffers(SDL_GPURenderPass *pass,
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
  SDL_GPUBuffer *storageBufs[8];
  if (usingMSL) {
    storageBufs[0] =
        gs.sourcePrimitiveIds ? gs.sourcePrimitiveIds.sdlBuffer() : dummyBuf;
    storageBufs[1] = gs.primitiveIds ? gs.primitiveIds.sdlBuffer() : dummyBuf;
    storageBufs[2] =
        gs.sourceVertexIds ? gs.sourceVertexIds.sdlBuffer() : dummyBuf;
    storageBufs[3] = gs.attr[0] ? gs.attr[0].sdlBuffer() : dummyBuf;
    storageBufs[4] = gs.attr[1] ? gs.attr[1].sdlBuffer() : dummyBuf;
    storageBufs[5] = gs.attr[2] ? gs.attr[2].sdlBuffer() : dummyBuf;
    storageBufs[6] = gs.attr[3] ? gs.attr[3].sdlBuffer() : dummyBuf;
    storageBufs[7] = gs.attr[4] ? gs.attr[4].sdlBuffer() : dummyBuf;
  } else {
    storageBufs[0] =
        gs.sourceVertexIds ? gs.sourceVertexIds.sdlBuffer() : dummyBuf;
    storageBufs[1] =
        gs.sourcePrimitiveIds ? gs.sourcePrimitiveIds.sdlBuffer() : dummyBuf;
    storageBufs[2] = gs.primitiveIds ? gs.primitiveIds.sdlBuffer() : dummyBuf;
    storageBufs[3] = gs.attr[0] ? gs.attr[0].sdlBuffer() : dummyBuf;
    storageBufs[4] = gs.attr[1] ? gs.attr[1].sdlBuffer() : dummyBuf;
    storageBufs[5] = gs.attr[2] ? gs.attr[2].sdlBuffer() : dummyBuf;
    storageBufs[6] = gs.attr[3] ? gs.attr[3].sdlBuffer() : dummyBuf;
    storageBufs[7] = gs.attr[4] ? gs.attr[4].sdlBuffer() : dummyBuf;
  }
  SDL_BindGPUVertexStorageBuffers(pass, 0, storageBufs, 8);

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
      uvec4(gs.attrMask,
          gs.hasSourcePrimitiveIds ? 1u : 0u,
          gs.hasPrimitiveIds ? 1u : 0u,
          0u),
      uvec4(gs.attrKind, gs.packedComponents, 0u, 0u)};
  SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));
}

void Curve::issueDrawCall(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const SurfaceDrawContext &ctx)
{
  auto &gs = m_gpuState;
  auto *ds = deviceState();

  if (!ds->gpu.triangleMeshPipeline || !gs.vertexData)
    return;

  SDL_DrawGPUPrimitives(pass, gs.vertexCount, 1, 0, 0);
}

GeometryAttrInfo Curve::attrInfo() const
{
  return {m_gpuState.attrMask,
      m_gpuState.attrKind,
      m_gpuState.packedComponents,
      m_gpuState.uniformAttr};
}

void Curve::gpu_finalizeGeometry()
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
  const bool hasPerVertexRadius = m_radii && m_radii->size() >= numVerts;
  const float *radii = hasPerVertexRadius ? m_radii->beginAs<float>() : nullptr;

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
  vertexData.reserve(numPrims * kCurveSides * (6 + 6 * kCapBands) * 6);
  sourceVertexIds.reserve(numPrims * kCurveSides * (6 + 6 * kCapBands));
  sourcePrimitiveIds.reserve(numPrims * kCurveSides * (2 + 4 * kCapBands));

  auto appendVertex =
      [&](const vec3 &p, const vec3 &n, uint32_t sourceVertexId) {
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
    const uint32_t idx0 = readIndex(m_indices.get(), i);
    const uint32_t idx1 = idx0 + 1u;
    if (idx0 >= numVerts || idx1 >= numVerts)
      continue;

    const vec3 p0 = pos[idx0];
    const vec3 p1 = pos[idx1];
    const float r0 = std::max(0.f, radii ? radii[idx0] : m_uniformRadius);
    const float r1 = std::max(0.f, radii ? radii[idx1] : m_uniformRadius);

    if (!isFiniteVec3(p0) || !isFiniteVec3(p1) || !isFiniteFloat(r0)
        || !isFiniteFloat(r1) || (r0 <= 0.f && r1 <= 0.f)) {
      continue;
    }

    const vec3 axis = p1 - p0;
    const float axisLen2 = dot(axis, axis);
    if (axisLen2 <= 1e-20f)
      continue;

    const float axisLen = std::sqrt(axisLen2);
    const vec3 dir = axis / axisLen;
    const float deltaR = r1 - r0;
    const vec3 ref =
        glm::abs(dir.z) < 0.999f ? vec3(0.f, 0.f, 1.f) : vec3(0.f, 1.f, 0.f);
    const vec3 tangent = safeNormalize(cross(ref, dir), vec3(1.f, 0.f, 0.f));
    const vec3 bitangent =
        safeNormalize(cross(dir, tangent), vec3(0.f, 1.f, 0.f));

    const float angleStep = glm::two_pi<float>() / float(kCurveSides);
    for (uint32_t side = 0; side < kCurveSides; ++side) {
      const float a0 = angleStep * float(side);
      const float a1 = angleStep * float((side + 1) % kCurveSides);
      const vec3 radial0 = tangent * std::cos(a0) + bitangent * std::sin(a0);
      const vec3 radial1 = tangent * std::cos(a1) + bitangent * std::sin(a1);

      const vec3 n0 = sideNormal(radial0, dir, axisLen, deltaR);
      const vec3 n1 = sideNormal(radial1, dir, axisLen, deltaR);

      const bool hasFirstRing = r0 > kRadiusEpsilon;
      const bool hasSecondRing = r1 > kRadiusEpsilon;
      const vec3 p00 = p0 + radial0 * r0;
      const vec3 p01 = p0 + radial1 * r0;
      const vec3 p10 = p1 + radial0 * r1;
      const vec3 p11 = p1 + radial1 * r1;

      if (hasFirstRing && hasSecondRing) {
        appendTriangle(p00,
            n0,
            idx0,
            p10,
            n0,
            idx1,
            p11,
            n1,
            idx1,
            static_cast<uint32_t>(i));
        appendTriangle(p00,
            n0,
            idx0,
            p11,
            n1,
            idx1,
            p01,
            n1,
            idx0,
            static_cast<uint32_t>(i));
      } else if (hasFirstRing) {
        appendTriangle(p00,
            n0,
            idx0,
            p1,
            n0,
            idx1,
            p01,
            n1,
            idx0,
            static_cast<uint32_t>(i));
      } else if (hasSecondRing) {
        appendTriangle(p0,
            n0,
            idx0,
            p10,
            n0,
            idx1,
            p11,
            n1,
            idx1,
            static_cast<uint32_t>(i));
      }

      if (hasFirstRing) {
        const float bandStep = glm::half_pi<float>() / float(kCapBands);
        for (uint32_t band = 0; band < kCapBands; ++band) {
          const float theta0 = bandStep * float(band);
          const float theta1 = bandStep * float(band + 1);

          const float sin0 = std::sin(theta0);
          const float cos0 = std::cos(theta0);
          const float sin1 = std::sin(theta1);
          const float cos1 = std::cos(theta1);

          const vec3 outerN0 = safeNormalize(radial0 * sin0 - dir * cos0, -dir);
          const vec3 outerN1 = safeNormalize(radial1 * sin0 - dir * cos0, -dir);
          const vec3 innerN0 =
              safeNormalize(radial0 * sin1 - dir * cos1, radial0);
          const vec3 innerN1 =
              safeNormalize(radial1 * sin1 - dir * cos1, radial1);

          const vec3 outerP0 = p0 + outerN0 * r0;
          const vec3 outerP1 = p0 + outerN1 * r0;
          const vec3 innerP0 = p0 + innerN0 * r0;
          const vec3 innerP1 = p0 + innerN1 * r0;

          if (band == 0) {
            appendTriangle(outerP0,
                outerN0,
                idx0,
                innerP1,
                innerN1,
                idx0,
                innerP0,
                innerN0,
                idx0,
                static_cast<uint32_t>(i));
          } else {
            appendTriangle(outerP0,
                outerN0,
                idx0,
                outerP1,
                outerN1,
                idx0,
                innerP1,
                innerN1,
                idx0,
                static_cast<uint32_t>(i));
            appendTriangle(outerP0,
                outerN0,
                idx0,
                innerP1,
                innerN1,
                idx0,
                innerP0,
                innerN0,
                idx0,
                static_cast<uint32_t>(i));
          }
        }
      }

      if (hasSecondRing) {
        const float bandStep = glm::half_pi<float>() / float(kCapBands);
        for (uint32_t band = 0; band < kCapBands; ++band) {
          const float theta0 = bandStep * float(band);
          const float theta1 = bandStep * float(band + 1);

          const float sin0 = std::sin(theta0);
          const float cos0 = std::cos(theta0);
          const float sin1 = std::sin(theta1);
          const float cos1 = std::cos(theta1);

          const vec3 outerN0 = safeNormalize(radial0 * sin0 + dir * cos0, dir);
          const vec3 outerN1 = safeNormalize(radial1 * sin0 + dir * cos0, dir);
          const vec3 innerN0 =
              safeNormalize(radial0 * sin1 + dir * cos1, radial0);
          const vec3 innerN1 =
              safeNormalize(radial1 * sin1 + dir * cos1, radial1);

          const vec3 outerP0 = p1 + outerN0 * r1;
          const vec3 outerP1 = p1 + outerN1 * r1;
          const vec3 innerP0 = p1 + innerN0 * r1;
          const vec3 innerP1 = p1 + innerN1 * r1;

          if (band == 0) {
            appendTriangle(outerP0,
                outerN0,
                idx1,
                innerP0,
                innerN0,
                idx1,
                innerP1,
                innerN1,
                idx1,
                static_cast<uint32_t>(i));
          } else {
            appendTriangle(outerP0,
                outerN0,
                idx1,
                innerP0,
                innerN0,
                idx1,
                innerP1,
                innerN1,
                idx1,
                static_cast<uint32_t>(i));
            appendTriangle(outerP0,
                outerN0,
                idx1,
                innerP1,
                innerN1,
                idx1,
                outerP1,
                outerN1,
                idx1,
                static_cast<uint32_t>(i));
          }
        }
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
  m_gpuState.sourceVertexIds.upload(
      sourceVertexIds.data(), sourceVertexIds.size());

  if (!m_gpuState.sourcePrimitiveIds)
    m_gpuState.sourcePrimitiveIds =
        GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
  m_gpuState.sourcePrimitiveIds.upload(
      sourcePrimitiveIds.data(), sourcePrimitiveIds.size());

  m_gpuState.vertexCount = static_cast<uint32_t>(sourceVertexIds.size());
  m_gpuState.triangleCount = static_cast<uint32_t>(sourcePrimitiveIds.size());
  m_gpuState.hasSourcePrimitiveIds = true;
  m_gpuState.hasPrimitiveIds = readPrimitiveIdBuffer(
      m_gpuState.primitiveIds, dev, m_primitiveIds.get(), numPrims);

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

void Curve::gpu_freeGeometry()
{
  m_gpuState.vertexData = {};
  m_gpuState.sourceVertexIds = {};
  m_gpuState.sourcePrimitiveIds = {};
  m_gpuState.primitiveIds = {};
  for (int s = 0; s < NUM_ATTR_SLOTS; ++s)
    m_gpuState.attr[s] = {};
  m_gpuState.vertexCount = 0;
  m_gpuState.triangleCount = 0;
  m_gpuState.attrMask = 0;
  m_gpuState.attrKind = 0;
  m_gpuState.packedComponents = 0;
  m_gpuState.hasSourcePrimitiveIds = false;
  m_gpuState.hasPrimitiveIds = false;
}

} // namespace helide_gpu
