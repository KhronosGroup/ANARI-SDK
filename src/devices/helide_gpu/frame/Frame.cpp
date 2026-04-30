// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"
// std
#include <algorithm>
#include <chrono>
#include <cstring>

namespace helide_gpu {

// Helper functions ///////////////////////////////////////////////////////////

// Internal render targets always use linear formats; sRGB encoding is applied
// in the composite shader so that shading math stays in linear space.
static SDL_GPUTextureFormat anari_to_sdl_format(ANARIDataType format)
{
  switch (format) {
  case ANARI_FLOAT32_VEC4:
    return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
  case ANARI_UFIXED8_RGBA_SRGB:
  case ANARI_UFIXED8_VEC4:
  default:
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  }
}

// Frame definitions //////////////////////////////////////////////////////////

Frame::Frame(HelideGPUDeviceGlobalState *s) : helium::BaseFrame(s) {}

Frame::~Frame()
{
  wait();
  gpu_enqueue_method(&Frame::gpu_freeObjects).wait();
}

bool Frame::isValid() const
{
  return m_renderer && m_renderer->isValid() && m_camera && m_camera->isValid()
      && m_world && m_world->isValid() && m_size.x > 0 && m_size.y > 0;
}

HelideGPUDeviceGlobalState *Frame::deviceState() const
{
  return (HelideGPUDeviceGlobalState *)helium::BaseObject::m_state;
}

void Frame::commitParameters()
{
  m_renderer = getParamObject<Renderer>("renderer");
  m_camera = getParamObject<Camera>("camera");
  m_world = getParamObject<World>("world");
  m_colorType = getParam<anari::DataType>("channel.color", ANARI_UNKNOWN);
  m_depthType = getParam<anari::DataType>("channel.depth", ANARI_UNKNOWN);
  m_primIdType =
      getParam<anari::DataType>("channel.primitiveId", ANARI_UNKNOWN);
  m_objIdType = getParam<anari::DataType>("channel.objectId", ANARI_UNKNOWN);
  m_instIdType = getParam<anari::DataType>("channel.instanceId", ANARI_UNKNOWN);
  m_albedoType = getParam<anari::DataType>("channel.albedo", ANARI_UNKNOWN);
  m_normalType = getParam<anari::DataType>("channel.normal", ANARI_UNKNOWN);
  m_size = getParam<uvec2>("size", uvec2(0u, 0u));
}

void Frame::finalize()
{
  if (!m_renderer) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'renderer' on frame");
  }

  if (!m_camera) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'camera' on frame");
  }

  if (!m_world) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'world' on frame");
  }

  if (m_size.x == 0 || m_size.y == 0)
    reportMessage(ANARI_SEVERITY_WARNING, "invalid frame dimensions");

  gpu_enqueue_method(&Frame::gpu_allocateObjects);
}

bool Frame::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, m_duration);
    return true;
  }

  return 0;
}

void Frame::renderFrame()
{
  m_renderStartTime = std::chrono::steady_clock::now();
  wait();
  m_future = gpu_enqueue_method(&Frame::gpu_renderFrame);
}

void *Frame::map(std::string_view channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  wait();

  *width = m_size.x;
  *height = m_size.y;
  void *ptr = nullptr;

  if (!isValid())
    return ptr;

  if (channel == "channel.color") {
    *pixelType = m_colorType;
    gpu_enqueue_method(&Frame::gpu_mapColorBuffer).wait();
    ptr = m_gpuState.mappedColorPtr;
  } else if (channel == "channel.depth" && m_depthType != ANARI_UNKNOWN) {
    *pixelType = ANARI_FLOAT32;
    gpu_enqueue_method(&Frame::gpu_mapDepthBuffer).wait();
    ptr = m_gpuState.mappedDepthPtr;
  } else if ((channel == "channel.primitiveId" || channel == "channel.objectId"
                 || channel == "channel.instanceId")
      && (m_primIdType != ANARI_UNKNOWN || m_objIdType != ANARI_UNKNOWN
          || m_instIdType != ANARI_UNKNOWN)) {
    *pixelType = ANARI_UINT32;
    gpu_enqueue_method(&Frame::gpu_mapIdsBuffer).wait();
    if (channel == "channel.primitiveId")
      ptr = m_primIdStaging.data();
    else if (channel == "channel.objectId")
      ptr = m_objIdStaging.data();
    else
      ptr = m_instIdStaging.data();
  } else if (channel == "channel.albedo" && m_albedoType != ANARI_UNKNOWN) {
    *pixelType = ANARI_FLOAT32_VEC3;
    gpu_enqueue_method(&Frame::gpu_mapAlbedoBuffer).wait();
    ptr = m_albedoStaging.data();
  } else if (channel == "channel.normal" && m_normalType != ANARI_UNKNOWN) {
    *pixelType = ANARI_FLOAT32_VEC3;
    gpu_enqueue_method(&Frame::gpu_mapNormalBuffer).wait();
    ptr = m_normalStaging.data();
  } else {
    *width = 0;
    *height = 0;
    *pixelType = ANARI_UNKNOWN;
  }

  return ptr;
}

void Frame::unmap(std::string_view channel)
{
  if (channel == "channel.color" && m_gpuState.mappedColorPtr)
    gpu_enqueue_method(&Frame::gpu_unmapColorBuffer);
  else if (channel == "channel.depth" && m_gpuState.mappedDepthPtr)
    gpu_enqueue_method(&Frame::gpu_unmapDepthBuffer);
  else if ((channel == "channel.primitiveId" || channel == "channel.objectId"
               || channel == "channel.instanceId")
      && (m_primIdType != ANARI_UNKNOWN || m_objIdType != ANARI_UNKNOWN
          || m_instIdType != ANARI_UNKNOWN))
    gpu_enqueue_method(&Frame::gpu_unmapIdsBuffer);
  else if (channel == "channel.albedo" && m_albedoType != ANARI_UNKNOWN)
    gpu_enqueue_method(&Frame::gpu_unmapAlbedoBuffer);
  else if (channel == "channel.normal" && m_normalType != ANARI_UNKNOWN)
    gpu_enqueue_method(&Frame::gpu_unmapNormalBuffer);
}

int Frame::frameReady(ANARIWaitMask m)
{
  if (m == ANARI_NO_WAIT)
    return ready();
  else {
    wait();
    return 1;
  }
}

void Frame::discard()
{
  // no-op
}

bool Frame::ready() const
{
  return helium::tasking::isReady(m_future);
}

void Frame::wait() const
{
  helium::tasking::wait(m_future);
}

void Frame::gpu_allocateObjects()
{
  reportMessage(ANARI_SEVERITY_DEBUG, "Frame reallocating GPU objects");

  auto &state = *deviceState();
  auto *dev = state.gpu.device;
  if (!dev || m_size.x == 0 || m_size.y == 0)
    return;

  gpu_freeObjects();

  const uint32_t width = m_size.x;
  const uint32_t height = m_size.y;

  // Color render target (SAMPLER usage for SSAO composite reads)
  m_gpuState.colorTarget = GPUTexture(dev,
      anari_to_sdl_format(m_colorType),
      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
  m_gpuState.colorTarget.alloc(width, height);

  // Depth render target (SAMPLER usage for SSAO depth reads)
  m_gpuState.depthTarget = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
      SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
  m_gpuState.depthTarget.alloc(width, height);

  // Packed ID render target: RGBA32_UINT (R=primId, G=objId, B=instId)
  m_gpuState.idsTarget = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT,
      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
  m_gpuState.idsTarget.alloc(width, height);

  // Albedo and normal render targets (always created -- required by MRT
  // pipeline). Normal needs SAMPLER usage for SSAO.
  m_gpuState.albedoTarget = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
  m_gpuState.albedoTarget.alloc(width, height);

  m_gpuState.normalTarget = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
  m_gpuState.normalTarget.alloc(width, height);

  // SSAO post-process targets
  m_gpuState.aoTarget = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
  m_gpuState.aoTarget.alloc(width, height);

  m_gpuState.aoBlurTarget = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
  m_gpuState.aoBlurTarget.alloc(width, height);

  // Composite output (same format as color target)
  m_gpuState.colorTargetB = GPUTexture(
      dev, anari_to_sdl_format(m_colorType), SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
  m_gpuState.colorTargetB.alloc(width, height);
}

void Frame::gpu_freeObjects()
{
  m_gpuState.colorTarget = {};
  m_gpuState.depthTarget = {};
  m_gpuState.idsTarget = {};
  m_gpuState.albedoTarget = {};
  m_gpuState.normalTarget = {};
  m_gpuState.aoTarget = {};
  m_gpuState.aoBlurTarget = {};
  m_gpuState.colorTargetB = {};
  m_gpuState.lastColorDownload = nullptr;
}

void Frame::gpu_renderFrame()
{
  auto &state = *deviceState();
  state.commitBuffer.flush();

  if (!isValid()) {
    reportMessage(
        ANARI_SEVERITY_ERROR, "skipping render of incomplete frame object");
    return;
  }

  if (state.commitBuffer.lastObjectFinalization() <= m_frameLastRendered)
    return;

  m_frameLastRendered = helium::newTimeStamp();

  auto *dev = state.gpu.device;
  if (!dev || !m_gpuState.colorTarget)
    return;

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
  if (!cmd)
    return;

  gpu_renderFrame_geometryPass(cmd);

  auto *cam = dynamic_cast<Camera *>(m_camera.ptr);
  auto &gpuPipelines = deviceState()->gpu;

  const bool doSSAO = m_renderer && m_renderer->ssaoEnabled()
      && gpuPipelines.ssaoPipeline && gpuPipelines.compositePipeline
      && m_gpuState.aoTarget && m_gpuState.colorTargetB && cam;

  // The composite pass also applies sRGB output encoding, so run it whenever
  // the application requested an sRGB frame channel even without SSAO.
  const bool needsSRGB = (m_colorType == ANARI_UFIXED8_RGBA_SRGB);
  const bool doComposite = doSSAO
      || (needsSRGB && m_renderer && gpuPipelines.compositePipeline
          && m_gpuState.colorTargetB);

  if (doSSAO)
    gpu_renderFrame_ssaoPass(cmd, cam);
  if (doComposite)
    gpu_renderFrame_compositePass(cmd, doSSAO, needsSRGB);

  gpu_renderFrame_downloadPass(cmd, doComposite);

  m_renderEndTime = std::chrono::steady_clock::now();
  m_duration =
      std::chrono::duration<float>(m_renderEndTime - m_renderStartTime).count();
}

void Frame::gpu_renderFrame_geometryPass(SDL_GPUCommandBuffer *cmd)
{
  auto bgc = m_renderer ? m_renderer->background() : vec4(1.f, 0.f, 1.f, 1.f);

  // Render pass: clear color, packed IDs (~0u), albedo (0), normal (0)
  float idClearVal;
  uint32_t idClearBits = ~0u;
  std::memcpy(&idClearVal, &idClearBits, sizeof(float));

  SDL_GPUColorTargetInfo colorInfos[4]{};
  colorInfos[0].texture = m_gpuState.colorTarget.sdlTexture();
  colorInfos[0].clear_color = {bgc.x, bgc.y, bgc.z, bgc.w};
  colorInfos[0].load_op = SDL_GPU_LOADOP_CLEAR;
  colorInfos[0].store_op = SDL_GPU_STOREOP_STORE;

  colorInfos[1].texture = m_gpuState.idsTarget.sdlTexture();
  colorInfos[1].clear_color = {idClearVal, idClearVal, idClearVal, idClearVal};
  colorInfos[1].load_op = SDL_GPU_LOADOP_CLEAR;
  colorInfos[1].store_op = SDL_GPU_STOREOP_STORE;

  colorInfos[2].texture = m_gpuState.albedoTarget.sdlTexture();
  colorInfos[2].clear_color = {0, 0, 0, 0};
  colorInfos[2].load_op = SDL_GPU_LOADOP_CLEAR;
  colorInfos[2].store_op = SDL_GPU_STOREOP_STORE;

  colorInfos[3].texture = m_gpuState.normalTarget.sdlTexture();
  colorInfos[3].clear_color = {0, 0, 0, 0};
  colorInfos[3].load_op = SDL_GPU_LOADOP_CLEAR;
  colorInfos[3].store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPUDepthStencilTargetInfo depthInfo{};
  depthInfo.texture = m_gpuState.depthTarget.sdlTexture();
  depthInfo.clear_depth = 1.0f;
  depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;
  depthInfo.store_op = SDL_GPU_STOREOP_STORE;
  depthInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  depthInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

  SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(
      cmd, colorInfos, 4, m_gpuState.depthTarget ? &depthInfo : nullptr);

  auto *cam = dynamic_cast<Camera *>(m_camera.ptr);
  if (cam && m_renderer && m_world) {
    const mat4 V = cam->viewMatrix();
    const float frameAspect = float(m_size.x) / float(m_size.y);
    const mat4 P = cam->projMatrix(frameAspect);

    SDL_GPUViewport viewport{};
    viewport.x = 0.f;
    viewport.y = 0.f;
    viewport.w = static_cast<float>(m_size.x);
    viewport.h = static_cast<float>(m_size.y);
    viewport.min_depth = 0.f;
    viewport.max_depth = 1.f;
    SDL_SetGPUViewport(pass, &viewport);

    SDL_Rect scissor{
        0, 0, static_cast<int>(m_size.x), static_cast<int>(m_size.y)};
    SDL_SetGPUScissor(pass, &scissor);

    for (auto *instance : m_world->instances()) {
      if (!instance || !instance->isValid())
        continue;
      auto *grp = instance->group();
      if (!grp)
        continue;

      for (size_t t = 0; t < instance->numTransforms(); t++) {
        const mat4 M = instance->xfm(t);
        const mat4 MV = V * M;
        const mat4 MVP = P * MV;

        for (auto *surface : grp->surfaces()) {
          if (!surface || !surface->isValid())
            continue;

          SurfaceDrawContext ctx{};
          ctx.MVP = MVP;
          ctx.MV = MV;
          ctx.M = M;
          ctx.P = P;
          ctx.ambientRadiance = m_renderer->ambientRadiance();
          ctx.eyeLightBlendRatio = m_renderer->eyeLightBlendRatio();
          ctx.objectId = surface->id();
          ctx.instanceId = instance->id(t);
          surface->draw(pass, cmd, ctx);
        }
      }
    }

    // --- Volume rendering (after all opaque surfaces) ---
    if (deviceState()->gpu.volumePipeline) {
      const vec3 camWorldPos = vec3(glm::inverse(V) * vec4(0, 0, 0, 1));
      static uint32_t frameCounter = 0;
      const float jitterSeed = float(frameCounter++ % 1024);

      for (auto *instance : m_world->instances()) {
        if (!instance || !instance->isValid())
          continue;
        auto *grp = instance->group();
        if (!grp)
          continue;

        for (size_t t = 0; t < instance->numTransforms(); t++) {
          for (auto *vol : grp->volumes()) {
            if (!vol || !vol->isValid())
              continue;

            VolumeDrawContext ctx{};
            ctx.M = instance->xfm(t);
            ctx.V = V;
            ctx.P = P;
            ctx.camWorldPos = camWorldPos;
            ctx.objectId = vol->id();
            ctx.instanceId = instance->id(t);
            ctx.jitterSeed = jitterSeed;
            vol->draw(pass, cmd, ctx);
          }
        }
      }
    }
  }

  SDL_EndGPURenderPass(pass);
}

void Frame::gpu_renderFrame_ssaoPass(
    SDL_GPUCommandBuffer *cmd, const Camera *cam)
{
  auto &state = *deviceState();

  const float frameAspect = float(m_size.x) / float(m_size.y);
  const mat4 P = cam->projMatrix(frameAspect);
  const mat4 V = cam->viewMatrix();

  SDL_GPUViewport viewport{};
  viewport.x = 0.f;
  viewport.y = 0.f;
  viewport.w = static_cast<float>(m_size.x);
  viewport.h = static_cast<float>(m_size.y);
  viewport.min_depth = 0.f;
  viewport.max_depth = 1.f;

  SDL_Rect scissor{
      0, 0, static_cast<int>(m_size.x), static_cast<int>(m_size.y)};

  // --- SSAO pass ---
  {
    SDL_GPUColorTargetInfo aoColorInfo{};
    aoColorInfo.texture = m_gpuState.aoTarget.sdlTexture();
    aoColorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    aoColorInfo.clear_color = {1, 1, 1, 1}; // default: no occlusion
    aoColorInfo.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass *ssaoPass =
        SDL_BeginGPURenderPass(cmd, &aoColorInfo, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(ssaoPass, deviceState()->gpu.ssaoPipeline);
    SDL_SetGPUViewport(ssaoPass, &viewport);
    SDL_SetGPUScissor(ssaoPass, &scissor);

    SDL_GPUTextureSamplerBinding ssaoSamplers[4]{};
    ssaoSamplers[0].texture = m_gpuState.depthTarget.sdlTexture();
    ssaoSamplers[0].sampler = state.gpu.linearClampSampler;
    ssaoSamplers[1].texture = m_gpuState.normalTarget.sdlTexture();
    ssaoSamplers[1].sampler = state.gpu.linearClampSampler;
    ssaoSamplers[2].texture = state.gpu.ssaoNoiseTex;
    ssaoSamplers[2].sampler = state.gpu.ssaoNoiseSampler;
    ssaoSamplers[3].texture = m_gpuState.idsTarget.sdlTexture();
    ssaoSamplers[3].sampler = state.gpu.nearestClampSampler;
    SDL_BindGPUFragmentSamplers(ssaoPass, 0, ssaoSamplers, 4);

    struct SSAOUniforms
    {
      mat4 projection;
      mat4 invProjection;
      mat4 view;
      vec4 samples[64];
      float radius;
      float bias;
      float intensity;
      int kernelSize;
      vec2 noiseScale;
      vec2 _pad;
    } ssaoU{};

    ssaoU.projection = P;
    ssaoU.invProjection = glm::inverse(P);
    ssaoU.view = V;
    std::memcpy(ssaoU.samples, m_renderer->ssaoKernel(), 64 * sizeof(vec4));
    ssaoU.radius = m_renderer->ssaoRadius();
    ssaoU.bias = m_renderer->ssaoBias();
    ssaoU.intensity = m_renderer->ssaoIntensity();
    ssaoU.kernelSize = m_renderer->ssaoKernelSize();
    ssaoU.noiseScale = vec2(m_size.x / 4.0f, m_size.y / 4.0f);

    SDL_PushGPUFragmentUniformData(cmd, 0, &ssaoU, sizeof(ssaoU));

    SDL_DrawGPUPrimitives(ssaoPass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(ssaoPass);
  }

  // --- Blur pass (optional) ---
  if (m_renderer->ssaoBlurEnabled() && deviceState()->gpu.blurPipeline
      && m_gpuState.aoBlurTarget) {
    SDL_GPUColorTargetInfo blurColorInfo{};
    blurColorInfo.texture = m_gpuState.aoBlurTarget.sdlTexture();
    blurColorInfo.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blurColorInfo.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass *blurPass =
        SDL_BeginGPURenderPass(cmd, &blurColorInfo, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(blurPass, deviceState()->gpu.blurPipeline);
    SDL_SetGPUViewport(blurPass, &viewport);
    SDL_SetGPUScissor(blurPass, &scissor);

    SDL_GPUTextureSamplerBinding blurSamplers[3]{};
    blurSamplers[0].texture = m_gpuState.aoTarget.sdlTexture();
    blurSamplers[0].sampler = state.gpu.linearClampSampler;
    blurSamplers[1].texture = m_gpuState.depthTarget.sdlTexture();
    blurSamplers[1].sampler = state.gpu.linearClampSampler;
    blurSamplers[2].texture = m_gpuState.idsTarget.sdlTexture();
    blurSamplers[2].sampler = state.gpu.nearestClampSampler;
    SDL_BindGPUFragmentSamplers(blurPass, 0, blurSamplers, 3);

    struct BlurUniforms
    {
      vec2 texelSize;
      int blurRadius;
      int _pad;
    } blurU{};
    blurU.texelSize = vec2(1.0f / m_size.x, 1.0f / m_size.y);
    blurU.blurRadius = 2;

    SDL_PushGPUFragmentUniformData(cmd, 0, &blurU, sizeof(blurU));

    SDL_DrawGPUPrimitives(blurPass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(blurPass);
  }
}

void Frame::gpu_renderFrame_compositePass(
    SDL_GPUCommandBuffer *cmd, bool doSSAO, bool needsSRGB)
{
  auto &state = *deviceState();

  SDL_GPUViewport viewport{};
  viewport.x = 0.f;
  viewport.y = 0.f;
  viewport.w = static_cast<float>(m_size.x);
  viewport.h = static_cast<float>(m_size.y);
  viewport.min_depth = 0.f;
  viewport.max_depth = 1.f;

  SDL_Rect scissor{
      0, 0, static_cast<int>(m_size.x), static_cast<int>(m_size.y)};

  SDL_GPUColorTargetInfo compColorInfo{};
  compColorInfo.texture = m_gpuState.colorTargetB.sdlTexture();
  compColorInfo.load_op = SDL_GPU_LOADOP_DONT_CARE;
  compColorInfo.store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPURenderPass *compPass =
      SDL_BeginGPURenderPass(cmd, &compColorInfo, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(compPass, deviceState()->gpu.compositePipeline);
  SDL_SetGPUViewport(compPass, &viewport);
  SDL_SetGPUScissor(compPass, &scissor);

  SDL_GPUTextureSamplerBinding compSamplers[2]{};
  compSamplers[0].texture = m_gpuState.colorTarget.sdlTexture();
  compSamplers[0].sampler = state.gpu.linearClampSampler;
  // When SSAO ran, use the (possibly blurred) AO result; otherwise bind the
  // color target as a dummy (AO value is unused, shader reads ao=1.0 via
  // the uniform flag).
  SDL_GPUTexture *aoTex = m_gpuState.colorTarget.sdlTexture(); // dummy fallback
  if (doSSAO) {
    aoTex = (m_renderer->ssaoBlurEnabled() && m_gpuState.aoBlurTarget)
        ? m_gpuState.aoBlurTarget.sdlTexture()
        : m_gpuState.aoTarget.sdlTexture();
  }
  compSamplers[1].texture = aoTex;
  compSamplers[1].sampler = state.gpu.linearClampSampler;
  SDL_BindGPUFragmentSamplers(compPass, 0, compSamplers, 2);

  struct CompositeUniforms
  {
    int applyAO;
    int applySRGB;
    int _pad0;
    int _pad1;
  } compU{};
  compU.applyAO = doSSAO ? 1 : 0;
  compU.applySRGB = needsSRGB ? 1 : 0;
  SDL_PushGPUFragmentUniformData(cmd, 0, &compU, sizeof(compU));

  SDL_DrawGPUPrimitives(compPass, 3, 1, 0, 0);
  SDL_EndGPURenderPass(compPass);
}

void Frame::gpu_renderFrame_downloadPass(
    SDL_GPUCommandBuffer *cmd, bool doComposite)
{
  auto &state = *deviceState();
  auto *dev = state.gpu.device;

  // Copy pass: download textures to transfer buffers
  SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmd);

  // Download from composited output if composite ran, else original color
  auto &finalColor =
      doComposite ? m_gpuState.colorTargetB : m_gpuState.colorTarget;
  finalColor.enqueueDownload(copyPass);
  m_gpuState.lastColorDownload = &finalColor;

  if (m_depthType != ANARI_UNKNOWN)
    m_gpuState.depthTarget.enqueueDownload(copyPass);

  const bool needIds = m_primIdType != ANARI_UNKNOWN
      || m_objIdType != ANARI_UNKNOWN || m_instIdType != ANARI_UNKNOWN;
  if (needIds)
    m_gpuState.idsTarget.enqueueDownload(copyPass);
  if (m_albedoType != ANARI_UNKNOWN)
    m_gpuState.albedoTarget.enqueueDownload(copyPass);
  if (m_normalType != ANARI_UNKNOWN)
    m_gpuState.normalTarget.enqueueDownload(copyPass);

  SDL_EndGPUCopyPass(copyPass);

  SDL_SubmitGPUCommandBuffer(cmd);

  // Wait for all GPU work to complete so the transfer buffer is ready to map.
  SDL_WaitForGPUIdle(dev);
}

void Frame::gpu_mapColorBuffer()
{
  if (!m_gpuState.lastColorDownload)
    return;
  m_gpuState.mappedColorPtr = m_gpuState.lastColorDownload->mapDownload();
}

void Frame::gpu_unmapColorBuffer()
{
  if (!m_gpuState.lastColorDownload)
    return;
  m_gpuState.lastColorDownload->unmapDownload();
  m_gpuState.mappedColorPtr = nullptr;
}

void Frame::gpu_mapDepthBuffer()
{
  m_gpuState.mappedDepthPtr = m_gpuState.depthTarget.mapDownload();
}

void Frame::gpu_unmapDepthBuffer()
{
  m_gpuState.depthTarget.unmapDownload();
  m_gpuState.mappedDepthPtr = nullptr;
}

void Frame::gpu_mapIdsBuffer()
{
  auto *packed = static_cast<const uvec4 *>(m_gpuState.idsTarget.mapDownload());
  if (!packed)
    return;

  // Split packed RGBA32_UINT into per-channel uint32 staging buffers
  const size_t numPixels = m_size.x * m_size.y;
  m_primIdStaging.resize(numPixels);
  m_objIdStaging.resize(numPixels);
  m_instIdStaging.resize(numPixels);
  for (size_t i = 0; i < numPixels; ++i) {
    m_primIdStaging[i] = packed[i].x;
    m_objIdStaging[i] = packed[i].y;
    m_instIdStaging[i] = packed[i].z;
  }

  m_gpuState.idsTarget.unmapDownload();
}

void Frame::gpu_unmapIdsBuffer()
{
  // Staging buffers are CPU-side; nothing to unmap on GPU
}

void Frame::gpu_mapAlbedoBuffer()
{
  auto *rgba = static_cast<const vec4 *>(m_gpuState.albedoTarget.mapDownload());
  if (!rgba)
    return;

  const size_t numPixels = m_size.x * m_size.y;
  m_albedoStaging.resize(numPixels);
  for (size_t i = 0; i < numPixels; ++i)
    m_albedoStaging[i] = vec3(rgba[i]);

  m_gpuState.albedoTarget.unmapDownload();
}

void Frame::gpu_unmapAlbedoBuffer()
{
  // Staging buffer is CPU-side; nothing to unmap on GPU
}

void Frame::gpu_mapNormalBuffer()
{
  auto *rgba = static_cast<const vec4 *>(m_gpuState.normalTarget.mapDownload());
  if (!rgba)
    return;

  const size_t numPixels = m_size.x * m_size.y;
  m_normalStaging.resize(numPixels);
  for (size_t i = 0; i < numPixels; ++i)
    m_normalStaging[i] = vec3(rgba[i]);

  m_gpuState.normalTarget.unmapDownload();
}

void Frame::gpu_unmapNormalBuffer()
{
  // Staging buffer is CPU-side; nothing to unmap on GPU
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Frame *);
