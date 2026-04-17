// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TransferFunction1D.h"
// helium
#include "helium/array/Array1D.h"
// std
#include <cstring>

namespace helide_gpu {

// Unit cube vertices: 12 triangles (36 vertices) covering [0,1]^3
// clang-format off
static const vec3 s_cubeVerts[36] = {
  // -Z face
  {0,0,0}, {1,1,0}, {1,0,0},  {0,0,0}, {0,1,0}, {1,1,0},
  // +Z face
  {0,0,1}, {1,0,1}, {1,1,1},  {0,0,1}, {1,1,1}, {0,1,1},
  // -X face
  {0,0,0}, {0,0,1}, {0,1,1},  {0,0,0}, {0,1,1}, {0,1,0},
  // +X face
  {1,0,0}, {1,1,1}, {1,0,1},  {1,0,0}, {1,1,0}, {1,1,1},
  // -Y face
  {0,0,0}, {1,0,0}, {1,0,1},  {0,0,0}, {1,0,1}, {0,0,1},
  // +Y face
  {0,1,0}, {0,1,1}, {1,1,1},  {0,1,0}, {1,1,1}, {1,1,0},
};
// clang-format on

// Linear interpolation helper for transfer function arrays
static float lerpScalar(const float *data, size_t count, float t)
{
  if (count == 0)
    return 0.f;
  if (count == 1)
    return data[0];
  const float idx = t * float(count - 1);
  const size_t i0 = size_t(glm::clamp(idx, 0.f, float(count - 2)));
  const size_t i1 = i0 + 1;
  const float frac = idx - float(i0);
  return glm::mix(data[i0], data[i1], frac);
}

static vec3 lerpVec3(const vec3 *data, size_t count, float t)
{
  if (count == 0)
    return vec3(1.f);
  if (count == 1)
    return data[0];
  const float idx = t * float(count - 1);
  const size_t i0 = size_t(glm::clamp(idx, 0.f, float(count - 2)));
  const size_t i1 = i0 + 1;
  const float frac = idx - float(i0);
  return glm::mix(data[i0], data[i1], frac);
}

static vec4 lerpVec4(const vec4 *data, size_t count, float t)
{
  if (count == 0)
    return vec4(1.f);
  if (count == 1)
    return data[0];
  const float idx = t * float(count - 1);
  const size_t i0 = size_t(glm::clamp(idx, 0.f, float(count - 2)));
  const size_t i1 = i0 + 1;
  const float frac = idx - float(i0);
  return glm::mix(data[i0], data[i1], frac);
}

TransferFunction1D::TransferFunction1D(HelideGPUDeviceGlobalState *s)
    : Volume(s), m_color(this), m_opacity(this)
{}

TransferFunction1D::~TransferFunction1D()
{
  gpu_enqueue_method(this, &TransferFunction1D::gpu_freeResources).wait();
}

void TransferFunction1D::commitParameters()
{
  Volume::commitParameters();
  m_color = getParamObject<Array1D>("color");
  m_uniformColor = vec4(1.f);
  getParam("color", ANARI_FLOAT32_VEC3, &m_uniformColor);
  getParam("color", ANARI_FLOAT32_VEC4, &m_uniformColor);
  m_opacity = getParamObject<Array1D>("opacity");
  m_uniformOpacity = getParam<float>("opacity", 1.f) * m_uniformColor.w;
  m_field = getParamObject<SpatialField>("value");
  m_unitDistance = getParam<float>("unitDistance", 1.f);
  m_valueRange = vec2(0.f, 1.f);
  getParam("valueRange", ANARI_FLOAT32_VEC2, &m_valueRange);
  getParam("valueRange", ANARI_FLOAT32_BOX1, &m_valueRange);
}

void TransferFunction1D::finalize()
{
  if (!m_field) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing parameter 'value' on transferFunction1D ANARIVolume");
    return;
  }

  if (m_unitDistance <= 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unitDistance must be positive on transferFunction1D volume, "
        "clamping to minimum");
    m_unitDistance = 1e-6f;
  }

  discretizeTF();
  gpu_enqueue_method(this, &TransferFunction1D::gpu_createResources);
}

bool TransferFunction1D::isValid() const
{
  return m_field && m_field->isValid();
}

box3 TransferFunction1D::bounds() const
{
  if (m_field)
    return m_field->bounds();
  return {};
}

SDL_GPUTexture *TransferFunction1D::fieldTexture() const
{
  return m_field ? m_field->gpuTexture() : nullptr;
}

SDL_GPUSampler *TransferFunction1D::fieldSampler() const
{
  return m_field ? m_field->gpuSampler() : nullptr;
}

SDL_GPUTexture *TransferFunction1D::tfTexture() const
{
  return m_gpuState.tfTexture.sdlTexture();
}

SDL_GPUSampler *TransferFunction1D::tfSampler() const
{
  return m_gpuState.tfSampler;
}

SDL_GPUBuffer *TransferFunction1D::cubeVertexBuffer() const
{
  return m_gpuState.cubeVB.sdlBuffer();
}

vec3 TransferFunction1D::fieldOrigin() const
{
  return m_field ? m_field->origin() : vec3(0.f);
}

vec3 TransferFunction1D::fieldSpacing() const
{
  return m_field ? m_field->spacing() : vec3(1.f);
}

uvec3 TransferFunction1D::fieldDims() const
{
  return m_field ? m_field->dims() : uvec3(0u);
}

float TransferFunction1D::stepSize() const
{
  return m_field ? m_field->stepSize() : 1.f;
}

float TransferFunction1D::unitDistance() const
{
  return m_unitDistance;
}

vec2 TransferFunction1D::valueRange() const
{
  return m_valueRange;
}

void TransferFunction1D::draw(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const VolumeDrawContext &ctx)
{
  auto *ds = deviceState();
  if (!ds->gpu.volumePipeline)
    return;

  auto *fieldTex = fieldTexture();
  auto *fieldSamp = fieldSampler();
  auto *tfTex = tfTexture();
  auto *tfSamp = tfSampler();
  auto *cubeVB = cubeVertexBuffer();
  if (!fieldTex || !fieldSamp || !tfTex || !tfSamp || !cubeVB)
    return;

  SDL_BindGPUGraphicsPipeline(pass, ds->gpu.volumePipeline);

  // Bind cube vertex buffer
  SDL_GPUBufferBinding vb{};
  vb.buffer = cubeVB;
  SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

  // Bind samplers: [0] = volume 3D field, [1] = TF 2D
  SDL_GPUTextureSamplerBinding tsbs[2]{};
  tsbs[0].texture = fieldTex;
  tsbs[0].sampler = fieldSamp;
  tsbs[1].texture = tfTex;
  tsbs[1].sampler = tfSamp;
  SDL_BindGPUFragmentSamplers(pass, 0, tsbs, 2);

  const vec3 origin = fieldOrigin();
  const uvec3 dims = fieldDims();
  const vec3 sp = fieldSpacing();
  const vec3 extent =
      vec3(float(dims.x - 1), float(dims.y - 1), float(dims.z - 1)) * sp;
  const mat4 fieldToWorld = glm::translate(mat4(1.f), origin)
      * glm::scale(mat4(1.f), extent);
  const mat4 M_vol = ctx.M * fieldToWorld;
  const mat4 MVP_vol = ctx.P * ctx.V * M_vol;

  // Push vertex uniforms: MVP + M
  struct
  {
    mat4 mvp;
    mat4 m;
  } vertU{MVP_vol, M_vol};
  SDL_PushGPUVertexUniformData(cmd, 0, &vertU, sizeof(vertU));

  // Push fragment uniforms: VolumeParams
  const mat4 invM = glm::inverse(M_vol);
  const vec3 localCamPos = vec3(invM * vec4(ctx.camWorldPos, 1.f));

  const float maxExtent = glm::max(extent.x, glm::max(extent.y, extent.z));
  const float localStep =
      maxExtent > 0.f ? stepSize() / maxExtent : 0.01f;

  const vec2 vr = valueRange();

  struct
  {
    vec4 localCamPos;
    vec4 fieldExtent;
    vec2 valueRange;
    float localStepSize;
    float oneOverUnitDistance;
    uint32_t objectId;
    uint32_t instanceId;
    float jitterSeed;
    float _pad;
  } fragU{};
  fragU.localCamPos = vec4(localCamPos, 0.f);
  fragU.fieldExtent = vec4(extent, 0.f);
  fragU.valueRange = vr;
  fragU.localStepSize = localStep;
  fragU.oneOverUnitDistance = 1.f / unitDistance();
  fragU.objectId = ctx.objectId;
  fragU.instanceId = ctx.instanceId;
  fragU.jitterSeed = ctx.jitterSeed;

  SDL_PushGPUFragmentUniformData(cmd, 0, &fragU, sizeof(fragU));

  SDL_DrawGPUPrimitives(pass, 36, 1, 0, 0);
}

void TransferFunction1D::discretizeTF()
{
  m_tf.resize(TF_DIM);

  for (int i = 0; i < TF_DIM; ++i) {
    const float t = float(i) / float(TF_DIM - 1);

    vec4 c = m_uniformColor;
    if (m_color) {
      if (m_color->elementType() == ANARI_FLOAT32_VEC3) {
        c = vec4(
            lerpVec3(m_color->beginAs<vec3>(), m_color->totalSize(), t), 1.f);
      } else if (m_color->elementType() == ANARI_FLOAT32_VEC4) {
        c = lerpVec4(m_color->beginAs<vec4>(), m_color->totalSize(), t);
      }
    }

    const float o = m_opacity
        ? lerpScalar(m_opacity->beginAs<float>(), m_opacity->totalSize(), t)
        : m_uniformOpacity;

    m_tf[i] = vec4(c.x, c.y, c.z, c.w * o);
  }
}

void TransferFunction1D::gpu_createResources()
{
  gpu_freeResources();

  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  // --- Transfer function texture (256x1 RGBA32F) ---
  if (!m_tf.empty()) {
    m_gpuState.tfTexture = GPUTexture(dev,
        SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
        SDL_GPU_TEXTUREUSAGE_SAMPLER);
    m_gpuState.tfTexture.upload(
        m_tf.data(), m_tf.size(), static_cast<uint32_t>(m_tf.size()));

    // TF sampler: linear, clamp
    SDL_GPUSamplerCreateInfo sampInfo{};
    sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_gpuState.tfSampler = SDL_CreateGPUSampler(dev, &sampInfo);
  }

  // --- Unit cube vertex buffer ---
  m_gpuState.cubeVB = GPUBuffer(dev, SDL_GPU_BUFFERUSAGE_VERTEX);
  m_gpuState.cubeVB.upload(s_cubeVerts, sizeof(s_cubeVerts) / sizeof(vec3));
}

void TransferFunction1D::gpu_freeResources()
{
  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;
  if (m_gpuState.tfSampler) {
    SDL_ReleaseGPUSampler(dev, m_gpuState.tfSampler);
    m_gpuState.tfSampler = nullptr;
  }
  m_gpuState.tfTexture = {};
  m_gpuState.cubeVB = {};
}

} // namespace helide_gpu
