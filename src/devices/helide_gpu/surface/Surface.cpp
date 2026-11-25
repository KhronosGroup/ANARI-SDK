// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"
#include "sampler/ImageSampler.h"
#include "sampler/PrimitiveSampler.h"
// std
#include <cstddef>

namespace helide_gpu {

Surface::Surface(HelideGPUDeviceGlobalState *s) : Object(ANARI_SURFACE, s) {}

void Surface::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
  m_geometry = getParamObject<Geometry>("geometry");
  m_material = getParamObject<Material>("material");
}

void Surface::finalize()
{
  if (!m_material)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'material' on ANARISurface");

  if (!m_geometry)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'geometry' on ANARISurface");
}

uint32_t Surface::id() const
{
  return m_id;
}

const Geometry *Surface::geometry() const
{
  return m_geometry.ptr;
}

const Material *Surface::material() const
{
  return m_material.ptr;
}

bool Surface::isValid() const
{
  return m_geometry && m_material && m_geometry->isValid()
      && m_material->isValid();
}

void Surface::draw(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const SurfaceDrawContext &ctx)
{
  m_geometry->bindPipelineAndBuffers(pass, cmd, ctx);
  pushFragmentUniforms(pass, cmd, ctx);
  m_geometry->issueDrawCall(pass, cmd, ctx);
}

namespace {

// Mirrors SamplerParams in shaders/material.glsl (std140)
struct SamplerParams
{
  mat4 inTransform;
  vec4 inOffset;
  mat4 outTransform;
  vec4 outOffset;
  int32_t mode; // 0=none, 2=image, 3=transform, 4=primitive
  int32_t inAttrIdx;
  uint32_t primitiveOffset;
  uint32_t arraySize;
};
static_assert(sizeof(SamplerParams) == 176, "must match the std140 layout");

// The material's samplers, in the order of SAMPLER_* in material.glsl
enum MaterialSamplerSlot
{
  SAMPLER_COLOR = 0,
  SAMPLER_METALLIC,
  SAMPLER_ROUGHNESS,
  SAMPLER_OPACITY,
  SAMPLER_EMISSIVE,
  SAMPLER_NORMAL,
  NUM_MATERIAL_SAMPLERS
};

// A sampler's shader parameters and its texture binding, a dummy texture for
// none (or one that isn't valid) and for transform samplers
void resolveSampler(const HelideGPUDeviceGlobalState &state,
    const Sampler *sampler,
    SamplerParams &params,
    SDL_GPUTextureSamplerBinding &binding)
{
  params = {};
  params.inTransform = mat4(1.f);
  params.outTransform = mat4(1.f);
  params.inAttrIdx = 1;
  binding.texture = state.gpu.dummyTexture;
  binding.sampler = state.gpu.dummySampler;

  if (!sampler || !sampler->isValid())
    return;

  params.mode = sampler->samplerMode();
  params.inAttrIdx = sampler->inAttrIndex();
  params.inTransform = sampler->inTransform();
  params.inOffset = sampler->inOffset();
  params.outTransform = sampler->outTransform();
  params.outOffset = sampler->outOffset();

  if (params.mode == 2) {
    auto *imgSampler = static_cast<const ImageSampler *>(sampler);
    binding.texture = imgSampler->gpuState().texture.sdlTexture();
    binding.sampler = imgSampler->gpuState().sampler;
  } else if (params.mode == 4) {
    auto *primSampler = static_cast<const PrimitiveSampler *>(sampler);
    binding.texture = primSampler->gpuState().texture.sdlTexture();
    binding.sampler = primSampler->gpuState().sampler;
    params.primitiveOffset =
        static_cast<uint32_t>(primSampler->primitiveOffset());
    params.arraySize = primSampler->arraySize();
  }
}

} // namespace

void Surface::pushFragmentUniforms(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const SurfaceDrawContext &ctx)
{
  auto &state = *deviceState();
  const auto *mat = m_material.ptr;

  const GeometryAttrInfo ai = m_geometry->attrInfo();

  // The material's samplers: bind their textures to fragment sampler slots
  // 0..5 and push their transforms to fragment uniform slot 1
  struct SamplerUniforms
  {
    SamplerParams p[NUM_MATERIAL_SAMPLERS];
  } sampUniforms{};
  SDL_GPUTextureSamplerBinding tsb[NUM_MATERIAL_SAMPLERS]{};
  const Sampler *samplers[NUM_MATERIAL_SAMPLERS] = {
      mat ? mat->colorSampler() : nullptr,
      mat ? mat->metallicSampler() : nullptr,
      mat ? mat->roughnessSampler() : nullptr,
      mat ? mat->opacitySampler() : nullptr,
      mat ? mat->emissiveSampler() : nullptr,
      mat ? mat->normalSampler() : nullptr};
  for (int i = 0; i < NUM_MATERIAL_SAMPLERS; ++i)
    resolveSampler(state, samplers[i], sampUniforms.p[i], tsb[i]);
  SDL_BindGPUFragmentSamplers(pass, 0, tsb, NUM_MATERIAL_SAMPLERS);
  SDL_PushGPUFragmentUniformData(cmd, 1, &sampUniforms, sizeof(sampUniforms));

  // Resolve a material's geometry attribute name to its slot index, -1 for
  // none
  auto attrSlot = [](const std::string &attr) -> int32_t {
    if (attr == "color")
      return 0;
    else if (attr == "attribute0")
      return 1;
    else if (attr == "attribute1")
      return 2;
    else if (attr == "attribute2")
      return 3;
    else if (attr == "attribute3")
      return 4;
    return -1;
  };

  // Fragment uniform slot 0: MaterialUniforms (shaders/material.glsl)
  struct FragUniforms
  {
    vec4 baseColor;
    vec4 uniformAttrData[NUM_ATTR_SLOTS];
    uint32_t attrMaskVal;
    int32_t colorAttrIdx;
    float opacity;
    int32_t alphaMode;
    float alphaCutoff;
    float metallic;
    float roughness;
    float ior;
    int32_t metallicAttrIdx;
    int32_t roughnessAttrIdx;
    int32_t _pad[2];
    vec4 emissive;
  } fragUniforms{};
  static_assert(offsetof(FragUniforms, emissive) % 16 == 0,
      "must match the std140 layout");

  fragUniforms.baseColor = vec4(mat ? mat->color() : vec3(1.f), 1.f);
  for (int s = 0; s < NUM_ATTR_SLOTS; ++s)
    fragUniforms.uniformAttrData[s] = ai.uniformAttr[s];
  fragUniforms.attrMaskVal = ai.attrMask;
  fragUniforms.colorAttrIdx = mat ? attrSlot(mat->colorAttribute()) : -1;
  fragUniforms.opacity = mat ? mat->opacity() : 1.f;
  fragUniforms.alphaMode = mat ? mat->alphaMode() : 0;
  fragUniforms.alphaCutoff = mat ? mat->alphaCutoff() : 0.5f;
  fragUniforms.metallic = mat ? mat->metallic() : 0.f;
  fragUniforms.roughness = mat ? mat->roughness() : 1.f;
  fragUniforms.ior = mat ? mat->ior() : 1.5f;
  fragUniforms.metallicAttrIdx = mat ? attrSlot(mat->metallicAttribute()) : -1;
  fragUniforms.roughnessAttrIdx =
      mat ? attrSlot(mat->roughnessAttribute()) : -1;
  fragUniforms.emissive = vec4(mat ? mat->emissive() : vec3(0.f), 0.f);

  SDL_PushGPUFragmentUniformData(cmd, 0, &fragUniforms, sizeof(fragUniforms));

  // Fragment uniform slot 2 (LightingUniforms) is pushed once per frame by
  // the frame

  // Fragment uniform slot 3: IDUniforms
  struct IDUniforms
  {
    uint32_t objectId;
    uint32_t instanceId;
  } idUniforms{};
  idUniforms.objectId = ctx.objectId;
  idUniforms.instanceId = ctx.instanceId;
  SDL_PushGPUFragmentUniformData(cmd, 3, &idUniforms, sizeof(idUniforms));
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Surface *);
