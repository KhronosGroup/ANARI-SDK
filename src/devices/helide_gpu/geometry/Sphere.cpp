// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sphere.h"
#include "icosphere.h"
// anari
#include <anari/frontend/type_utility.h>
// std
#include <cstring>

namespace helide_gpu {

// Sphere definitions //////////////////////////////////////////////////////////

Sphere::Sphere(HelideGPUDeviceGlobalState *s)
    : Geometry(s), m_positions(this), m_radii(this)
{
  m_vertexAttr.reserve(SPHERE_NUM_ATTR_SLOTS);
  m_primitiveAttr.reserve(SPHERE_NUM_ATTR_SLOTS);
  for (int i = 0; i < SPHERE_NUM_ATTR_SLOTS; ++i) {
    m_vertexAttr.emplace_back(this);
    m_primitiveAttr.emplace_back(this);
  }
}

Sphere::~Sphere()
{
  gpu_enqueue_method(this, &Sphere::gpu_freeGeometry).wait();
}

void Sphere::commitParameters()
{
  m_positions = getParamObject<Array1D>("vertex.position");
  m_radii = getParamObject<Array1D>("vertex.radius");
  m_uniformRadius = getParam<float>("radius", 0.01f);

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

void Sphere::finalize()
{
  if (!m_positions) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on sphere geometry");
  }

  gpu_enqueue_method(this, &Sphere::gpu_finalizeGeometry);
}

box3 Sphere::bounds() const
{
  box3 b;
  if (m_positions) {
    const auto *pos = m_positions->beginAs<vec3>();
    const float *radii = (m_radii && m_radii->size() >= m_positions->size())
        ? m_radii->beginAs<float>()
        : nullptr;
    for (size_t i = 0, n = m_positions->size(); i < n; ++i) {
      float r = radii ? radii[i] : m_uniformRadius;
      b.extend(pos[i] - vec3(r));
      b.extend(pos[i] + vec3(r));
    }
  }
  return b;
}

bool Sphere::isValid() const
{
  return m_gpuState.positions.valid();
}

const SphereGPUState &Sphere::gpuState() const
{
  return m_gpuState;
}

void Sphere::bindPipelineAndBuffers(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const SurfaceDrawContext &ctx)
{
  auto &ss = m_gpuState;
  auto *ds = deviceState();

  if (!ds->gpu.spherePipeline || !ss.positions)
    return;

  SDL_BindGPUGraphicsPipeline(pass, ds->gpu.spherePipeline);

  SDL_GPUBufferBinding vb{};
  vb.buffer = ss.icoVertexBuffer.sdlBuffer();
  SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

  SDL_GPUBufferBinding ib{};
  ib.buffer = ss.icoIndexBuffer.sdlBuffer();
  SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

  auto *dummyBuf = ds->gpu.dummyStorageBuffer;
  SDL_GPUBuffer *storageBufs[7] = {
      ss.positions.sdlBuffer(),
      ss.radii ? ss.radii.sdlBuffer() : dummyBuf,
      ss.attr[0] ? ss.attr[0].sdlBuffer() : dummyBuf,
      ss.attr[1] ? ss.attr[1].sdlBuffer() : dummyBuf,
      ss.attr[2] ? ss.attr[2].sdlBuffer() : dummyBuf,
      ss.attr[3] ? ss.attr[3].sdlBuffer() : dummyBuf,
      ss.attr[4] ? ss.attr[4].sdlBuffer() : dummyBuf,
  };
  SDL_BindGPUVertexStorageBuffers(pass, 0, storageBufs, 7);

  struct
  {
    mat4 mvp;
    mat4 mv;
    mat4 m;
    mat4 p;
    uvec4 sphereInfo;
    vec4 defaults;
  } u{ctx.MVP,
      ctx.MV,
      ctx.M,
      ctx.P,
      uvec4(ss.radii ? 1u : 0u, ss.attrMask, ss.attrKind, ss.packedComponents),
      vec4(ss.uniformRadius, 0.f, 0.f, 0.f)};
  SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));
}

void Sphere::issueDrawCall(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const SurfaceDrawContext &ctx)
{
  auto &ss = m_gpuState;
  auto *ds = deviceState();

  if (!ds->gpu.spherePipeline || !ss.positions)
    return;

  SDL_DrawGPUIndexedPrimitives(pass, ss.icoIndexCount, ss.sphereCount, 0, 0, 0);
}

GeometryAttrInfo Sphere::attrInfo() const
{
  return {m_gpuState.attrMask,
      m_gpuState.attrKind,
      m_gpuState.packedComponents,
      m_gpuState.uniformAttr};
}

void Sphere::gpu_finalizeGeometry()
{
  if (!m_positions) {
    gpu_freeGeometry();
    return;
  }

  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  const size_t numSpheres = m_positions->size();

  // --- Upload icosphere mesh (static — only once) ---
  if (!m_gpuState.icoVertexBuffer.valid()) {
    m_gpuState.icoVertexBuffer = GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_VERTEX);
    m_gpuState.icoVertexBuffer.upload(ico_vertices, sizeof(ico_vertices));
    m_gpuState.icoIndexBuffer = GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_INDEX);
    m_gpuState.icoIndexBuffer.upload(ico_indices, sizeof(ico_indices));
    m_gpuState.icoIndexCount = ico_index_count;
  }

  // Helper: upload array to buffer, resetting buffer if array is absent.
  auto syncBuffer =
      [dev](GPUBuffer &buf, const Array1D *arr, bool convertToFloat = false) {
        if (!arr || arr->size() == 0) {
          buf = {};
          return;
        }
        if (!buf)
          buf = GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
        buf.uploadArray(arr);
      };

  // --- Sync per-sphere source arrays ---
  syncBuffer(m_gpuState.positions, m_positions.get());
  syncBuffer(m_gpuState.radii, m_radii.get());

  m_gpuState.sphereCount = static_cast<uint32_t>(numSpheres);
  m_gpuState.uniformRadius = m_uniformRadius;

  // --- Sync attribute arrays ---
  m_gpuState.attrMask = 0;
  m_gpuState.attrKind = 0;
  m_gpuState.packedComponents = 0;

  for (int s = 0; s < SPHERE_NUM_ATTR_SLOTS; ++s) {
    const auto *vtxArr = m_vertexAttr[s].get();
    const auto *primArr = m_primitiveAttr[s].get();

    if (vtxArr && vtxArr->size() >= numSpheres) {
      syncBuffer(m_gpuState.attr[s], vtxArr, true);
      if (m_gpuState.attr[s].valid()) {
        m_gpuState.attrMask |= (1u << s);
        m_gpuState.packedComponents |=
            (anari::componentsOf(vtxArr->elementType()) << (s * 4));
      }
    } else if (primArr && primArr->size() > 0) {
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

void Sphere::gpu_freeGeometry()
{
  m_gpuState.icoVertexBuffer = {};
  m_gpuState.icoIndexBuffer = {};
  m_gpuState.icoIndexCount = 0;
  m_gpuState.positions = {};
  m_gpuState.radii = {};
  for (int s = 0; s < SPHERE_NUM_ATTR_SLOTS; ++s)
    m_gpuState.attr[s] = {};
  m_gpuState.sphereCount = 0;
  m_gpuState.attrMask = 0;
  m_gpuState.attrKind = 0;
  m_gpuState.packedComponents = 0;
}

} // namespace helide_gpu
