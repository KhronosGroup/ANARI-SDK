// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"
#include "sampler/ImageSampler.h"
#include "sampler/PrimitiveSampler.h"

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

void Surface::pushFragmentUniforms(SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *cmd,
    const SurfaceDrawContext &ctx)
{
  auto &state = *deviceState();
  const auto *mat = m_material.ptr;

  const GeometryAttrInfo ai = m_geometry->attrInfo();

  // Check for sampler on the material
  const auto *sampler = mat ? mat->colorSampler() : nullptr;
  const bool hasSampler = sampler && sampler->isValid();
  const int32_t samplerModeFromSampler = hasSampler ? sampler->samplerMode() : 0;

  // Resolve material color attribute name to slot index
  int32_t colorAttrIndex = -1;
  if (!hasSampler && mat && !mat->colorAttribute().empty()) {
    const auto &attr = mat->colorAttribute();
    if (attr == "color")
      colorAttrIndex = 0;
    else if (attr == "attribute0")
      colorAttrIndex = 1;
    else if (attr == "attribute1")
      colorAttrIndex = 2;
    else if (attr == "attribute2")
      colorAttrIndex = 3;
    else if (attr == "attribute3")
      colorAttrIndex = 4;
  }

  int32_t samplerMode = 0;
  int32_t texCoordAttrIdx = 1;
  if (hasSampler) {
    samplerMode = samplerModeFromSampler;
    texCoordAttrIdx = sampler->inAttrIndex();
  } else if (colorAttrIndex >= 0) {
    samplerMode = 1;
  }

  // Bind texture+sampler (real or dummy)
  SDL_GPUTextureSamplerBinding tsb{};
  if (samplerMode == 2) {
    auto *imgSampler = dynamic_cast<const ImageSampler *>(sampler);
    tsb.texture = imgSampler->gpuState().texture.sdlTexture();
    tsb.sampler = imgSampler->gpuState().sampler;
  } else if (samplerMode == 4) {
    auto *primSampler = dynamic_cast<const PrimitiveSampler *>(sampler);
    tsb.texture = primSampler->gpuState().texture.sdlTexture();
    tsb.sampler = primSampler->gpuState().sampler;
  } else {
    tsb.texture = state.gpu.dummyTexture;
    tsb.sampler = state.gpu.dummySampler;
  }
  SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);

  // Fragment uniform slot 0: MaterialUniforms
  struct FragUniforms
  {
    vec4 baseColor;
    vec4 uniformAttrData[NUM_ATTR_SLOTS];
    uint32_t attrMaskVal;
    int32_t colorAttrIdx;
    int32_t samplerModeVal;
    int32_t texCoordAttrIdxVal;
    float opacity;
    int32_t alphaMode;
    float alphaCutoff;
    float _pad0;
  } fragUniforms{};

  fragUniforms.baseColor = vec4(mat ? mat->color() : vec3(1.f), 1.f);
  for (int s = 0; s < NUM_ATTR_SLOTS; ++s)
    fragUniforms.uniformAttrData[s] = ai.uniformAttr[s];
  fragUniforms.attrMaskVal = ai.attrMask;
  fragUniforms.colorAttrIdx = colorAttrIndex;
  fragUniforms.samplerModeVal = samplerMode;
  fragUniforms.texCoordAttrIdxVal = texCoordAttrIdx;
  fragUniforms.opacity = mat ? mat->opacity() : 1.f;
  fragUniforms.alphaMode = mat ? mat->alphaMode() : 0;
  fragUniforms.alphaCutoff = mat ? mat->alphaCutoff() : 0.5f;

  SDL_PushGPUFragmentUniformData(cmd, 0, &fragUniforms, sizeof(fragUniforms));

  // Fragment uniform slot 1: SamplerTransforms
  struct SamplerTransformUniforms
  {
    mat4 inTransform;
    vec4 inOffset;
    mat4 outTransform;
    vec4 outOffset;
    uint32_t primitiveOffset;
    uint32_t arraySize;
    uint32_t _pad[2];
  } sampUniforms{};

  if (hasSampler) {
    sampUniforms.inTransform = sampler->inTransform();
    sampUniforms.inOffset = sampler->inOffset();
    sampUniforms.outTransform = sampler->outTransform();
    sampUniforms.outOffset = sampler->outOffset();
    if (samplerMode == 4) {
      auto *primSampler = dynamic_cast<const PrimitiveSampler *>(sampler);
      sampUniforms.primitiveOffset =
          static_cast<uint32_t>(primSampler->primitiveOffset());
      sampUniforms.arraySize = primSampler->arraySize();
    }
  } else {
    sampUniforms.inTransform = mat4(1.f);
    sampUniforms.inOffset = vec4(0.f);
    sampUniforms.outTransform = mat4(1.f);
    sampUniforms.outOffset = vec4(0.f);
  }

  SDL_PushGPUFragmentUniformData(cmd, 1, &sampUniforms, sizeof(sampUniforms));

  // Fragment uniform slot 2: RendererUniforms
  struct RendererUniforms
  {
    float eyeLightBlendRatio;
    float ambientRadiance;
  } rendUniforms{};
  rendUniforms.eyeLightBlendRatio = ctx.eyeLightBlendRatio;
  rendUniforms.ambientRadiance = ctx.ambientRadiance;
  SDL_PushGPUFragmentUniformData(cmd, 2, &rendUniforms, sizeof(rendUniforms));

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
