// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HelideGPUDevice.h"

#include "array/Array1D.h"
#include "array/Array2D.h"
#include "array/Array3D.h"
#include "array/ObjectArray.h"
#include "frame/Frame.h"
#include "spatial_field/SpatialField.h"

#include "anari_library_helide_gpu_queries.h"

#include "GLContextInterface.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace helide_gpu {

// Helper functions ///////////////////////////////////////////////////////////

static void gpu_device_init(HelideGPUDeviceGlobalState *state)
{
  auto &gpu = state->gpu;

  gpu.sdlDevice.init();
  gpu.device = gpu.sdlDevice.device;

  if (!gpu.device) {
    anariDeviceReportStatus(state->device,
        ANARI_SEVERITY_FATAL_ERROR,
        ANARI_STATUS_UNKNOWN_ERROR,
        "[SDL3_gpu] SDL_CreateGPUDevice failed: %s",
        SDL_GetError());
    return;
  }

  anariDeviceReportStatus(state->device,
      ANARI_SEVERITY_INFO,
      ANARI_STATUS_NO_ERROR,
      "[SDL3_gpu] Device created (backend: %s)",
      SDL_GetGPUDeviceDriver(gpu.device));

  // Populate extension list from static query; skip compressed-texture
  // extensions (no SDL3_gpu format queries implemented yet).
  const char **ext = query_extensions();
  for (int i = 0; ext[i] != nullptr; ++i) {
    if (strncmp("ANARI_EXT_SAMPLER_COMPRESSED_FORMAT_", ext[i], 36) == 0)
      continue; // skip until SDL3_gpu format support is queried
    gpu.extensions.push_back(ext[i]);
  }
  gpu.extensions.push_back(nullptr);

  // Create 1x1x1 white dummy texture + sampler for binding when no real sampler
  {
    SDL_GPUTextureCreateInfo texInfo{};
    texInfo.type = SDL_GPU_TEXTURETYPE_3D;
    texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texInfo.width = 1;
    texInfo.height = 1;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels = 1;
    texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    gpu.dummyTexture = SDL_CreateGPUTexture(gpu.device, &texInfo);

    if (gpu.dummyTexture) {
      // Upload a single white pixel
      SDL_GPUTransferBufferCreateInfo tbInfo{};
      tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
      tbInfo.size = 4;
      auto *tb = SDL_CreateGPUTransferBuffer(gpu.device, &tbInfo);
      if (tb) {
        auto *p = static_cast<uint8_t *>(
            SDL_MapGPUTransferBuffer(gpu.device, tb, false));
        if (p) {
          p[0] = p[1] = p[2] = p[3] = 255;
          SDL_UnmapGPUTransferBuffer(gpu.device, tb);
        }
        auto *cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
        auto *pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = tb;
        SDL_GPUTextureRegion dst{};
        dst.texture = gpu.dummyTexture;
        dst.w = 1;
        dst.h = 1;
        dst.d = 1;
        SDL_UploadToGPUTexture(pass, &src, &dst, false);
        SDL_EndGPUCopyPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_WaitForGPUIdle(gpu.device);
        SDL_ReleaseGPUTransferBuffer(gpu.device, tb);
      }
    }

    SDL_GPUSamplerCreateInfo sampInfo{};
    sampInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    sampInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    gpu.dummySampler = SDL_CreateGPUSampler(gpu.device, &sampInfo);
  }

  // Create 16-byte dummy storage buffer for unused SSBO bind slots
  {
    SDL_GPUBufferCreateInfo bufInfo{};
    bufInfo.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ
        | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ
        | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    bufInfo.size = 16;
    gpu.dummyStorageBuffer = SDL_CreateGPUBuffer(gpu.device, &bufInfo);
  }

  // Create a zero-filled dummy vertex buffer for the triangle pipeline.
  // The shader does not semantically use this data, but Metal still performs
  // the vertex fetch for the declared stage_in. Keep the buffer large enough
  // to cover typical test draws so we don't read past the end.
  {
    constexpr size_t kDummyVertexCount = 65536;
    std::vector<float> zeros(kDummyVertexCount * 3, 0.f);

    SDL_GPUBufferCreateInfo bufInfo{};
    bufInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufInfo.size = static_cast<uint32_t>(zeros.size() * sizeof(float));
    gpu.dummyVertexBuffer = SDL_CreateGPUBuffer(gpu.device, &bufInfo);

    if (gpu.dummyVertexBuffer) {
      SDL_GPUTransferBufferCreateInfo tbInfo{};
      tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
      tbInfo.size = bufInfo.size;
      auto *tb = SDL_CreateGPUTransferBuffer(gpu.device, &tbInfo);
      if (tb) {
        if (void *mapped = SDL_MapGPUTransferBuffer(gpu.device, tb, false)) {
          std::memcpy(mapped, zeros.data(), zeros.size() * sizeof(float));
          SDL_UnmapGPUTransferBuffer(gpu.device, tb);
        }

        auto *cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
        auto *pass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = tb;
        src.offset = 0;

        SDL_GPUBufferRegion dst{};
        dst.buffer = gpu.dummyVertexBuffer;
        dst.offset = 0;
        dst.size = bufInfo.size;

        SDL_UploadToGPUBuffer(pass, &src, &dst, false);
        SDL_EndGPUCopyPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_WaitForGPUIdle(gpu.device);
        SDL_ReleaseGPUTransferBuffer(gpu.device, tb);
      }
    }
  }

  // Create SSAO shared resources: 4x4 noise texture + samplers
  {
    // 4x4 noise texture with random tangent-plane vectors
    SDL_GPUTextureCreateInfo texInfo{};
    texInfo.type = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texInfo.width = 4;
    texInfo.height = 4;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels = 1;
    texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    gpu.ssaoNoiseTex = SDL_CreateGPUTexture(gpu.device, &texInfo);

    if (gpu.ssaoNoiseTex) {
      const int noiseSize = 4 * 4;
      float noiseData[noiseSize * 4]; // RGBA32F
      srand(42);
      for (int i = 0; i < noiseSize; ++i) {
        float x = (float(rand()) / float(RAND_MAX)) * 2.0f - 1.0f;
        float y = (float(rand()) / float(RAND_MAX)) * 2.0f - 1.0f;
        float len = std::sqrt(x * x + y * y);
        if (len > 0.0f) {
          x /= len;
          y /= len;
        }
        noiseData[i * 4 + 0] = x;
        noiseData[i * 4 + 1] = y;
        noiseData[i * 4 + 2] = 0.0f;
        noiseData[i * 4 + 3] = 0.0f;
      }

      SDL_GPUTransferBufferCreateInfo tbInfo{};
      tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
      tbInfo.size = sizeof(noiseData);
      auto *tb = SDL_CreateGPUTransferBuffer(gpu.device, &tbInfo);
      if (tb) {
        auto *p = static_cast<float *>(
            SDL_MapGPUTransferBuffer(gpu.device, tb, false));
        if (p) {
          std::memcpy(p, noiseData, sizeof(noiseData));
          SDL_UnmapGPUTransferBuffer(gpu.device, tb);
        }
        auto *cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
        auto *pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = tb;
        SDL_GPUTextureRegion dst{};
        dst.texture = gpu.ssaoNoiseTex;
        dst.w = 4;
        dst.h = 4;
        dst.d = 1;
        SDL_UploadToGPUTexture(pass, &src, &dst, false);
        SDL_EndGPUCopyPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_WaitForGPUIdle(gpu.device);
        SDL_ReleaseGPUTransferBuffer(gpu.device, tb);
      }
    }

    // Noise sampler: nearest filter, repeat wrap
    SDL_GPUSamplerCreateInfo noiseSampInfo{};
    noiseSampInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    noiseSampInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    noiseSampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    noiseSampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    noiseSampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    noiseSampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    gpu.ssaoNoiseSampler = SDL_CreateGPUSampler(gpu.device, &noiseSampInfo);

    // Linear clamp sampler for post-process texture reads
    SDL_GPUSamplerCreateInfo linearSampInfo{};
    linearSampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    linearSampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    linearSampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    linearSampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    linearSampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    linearSampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    gpu.linearClampSampler = SDL_CreateGPUSampler(gpu.device, &linearSampInfo);

    // Integer ID textures are not filterable, so bind them through a nearest
    // clamp sampler in the SSAO/blur passes.
    SDL_GPUSamplerCreateInfo nearestSampInfo = linearSampInfo;
    nearestSampInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    nearestSampInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    gpu.nearestClampSampler =
        SDL_CreateGPUSampler(gpu.device, &nearestSampInfo);
  }

  state->gpu_initPipelines();
}

static void gpu_device_release(HelideGPUDeviceGlobalState *state)
{
  auto &gpu = state->gpu;
  if (gpu.device)
    state->gpu_destroyPipelines();
  if (gpu.device) {
    if (gpu.nearestClampSampler) {
      SDL_ReleaseGPUSampler(gpu.device, gpu.nearestClampSampler);
      gpu.nearestClampSampler = nullptr;
    }
    if (gpu.linearClampSampler) {
      SDL_ReleaseGPUSampler(gpu.device, gpu.linearClampSampler);
      gpu.linearClampSampler = nullptr;
    }
    if (gpu.ssaoNoiseSampler) {
      SDL_ReleaseGPUSampler(gpu.device, gpu.ssaoNoiseSampler);
      gpu.ssaoNoiseSampler = nullptr;
    }
    if (gpu.ssaoNoiseTex) {
      SDL_ReleaseGPUTexture(gpu.device, gpu.ssaoNoiseTex);
      gpu.ssaoNoiseTex = nullptr;
    }
    if (gpu.dummyStorageBuffer) {
      SDL_ReleaseGPUBuffer(gpu.device, gpu.dummyStorageBuffer);
      gpu.dummyStorageBuffer = nullptr;
    }
    if (gpu.dummyVertexBuffer) {
      SDL_ReleaseGPUBuffer(gpu.device, gpu.dummyVertexBuffer);
      gpu.dummyVertexBuffer = nullptr;
    }
    if (gpu.dummySampler) {
      SDL_ReleaseGPUSampler(gpu.device, gpu.dummySampler);
      gpu.dummySampler = nullptr;
    }
    if (gpu.dummyTexture) {
      SDL_ReleaseGPUTexture(gpu.device, gpu.dummyTexture);
      gpu.dummyTexture = nullptr;
    }
  }
  gpu.sdlDevice.release();
  gpu.device = nullptr;
}

// API Objects ////////////////////////////////////////////////////////////////

ANARIArray1D HelideGPUDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems)
{
  initDevice();

  Array1DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems = numItems;

  if (anari::isObject(type))
    return (ANARIArray1D) new ObjectArray(deviceState(), md);
  else
    return (ANARIArray1D) new Array1D(deviceState(), md);
}

ANARIArray2D HelideGPUDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  initDevice();

  Array2DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;

  return (ANARIArray2D) new Array2D(deviceState(), md);
}

ANARIArray3D HelideGPUDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  initDevice();

  Array3DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;
  md.numItems3 = numItems3;

  return (ANARIArray3D) new Array3D(deviceState(), md);
}

ANARICamera HelideGPUDevice::newCamera(const char *subtype)
{
  initDevice();
  return (ANARICamera)Camera::createInstance(subtype, deviceState());
}

ANARIFrame HelideGPUDevice::newFrame()
{
  initDevice();
  return (ANARIFrame) new Frame(deviceState());
}

ANARIGeometry HelideGPUDevice::newGeometry(const char *subtype)
{
  initDevice();
  return (ANARIGeometry)Geometry::createInstance(subtype, deviceState());
}

ANARIGroup HelideGPUDevice::newGroup()
{
  initDevice();
  return (ANARIGroup) new Group(deviceState());
}

ANARIInstance HelideGPUDevice::newInstance(const char * /*subtype*/)
{
  initDevice();
  return (ANARIInstance) new Instance(deviceState());
}

ANARILight HelideGPUDevice::newLight(const char *subtype)
{
  initDevice();
  return (ANARILight)Light::createInstance(subtype, deviceState());
}

ANARIMaterial HelideGPUDevice::newMaterial(const char *subtype)
{
  initDevice();
  return (ANARIMaterial)Material::createInstance(subtype, deviceState());
}

ANARIRenderer HelideGPUDevice::newRenderer(const char *subtype)
{
  initDevice();
  return (ANARIRenderer)Renderer::createInstance(subtype, deviceState());
}

ANARISampler HelideGPUDevice::newSampler(const char *subtype)
{
  initDevice();
  return (ANARISampler)Sampler::createInstance(subtype, deviceState());
}

ANARISpatialField HelideGPUDevice::newSpatialField(const char *subtype)
{
  initDevice();
  return (ANARISpatialField)SpatialField::createInstance(
      subtype, deviceState());
}

ANARISurface HelideGPUDevice::newSurface()
{
  initDevice();
  return (ANARISurface) new Surface(deviceState());
}

ANARIVolume HelideGPUDevice::newVolume(const char *subtype)
{
  initDevice();
  return (ANARIVolume)Volume::createInstance(subtype, deviceState());
}

ANARIWorld HelideGPUDevice::newWorld()
{
  initDevice();
  return (ANARIWorld) new World(deviceState());
}

// Query functions ////////////////////////////////////////////////////////////

const char **HelideGPUDevice::getObjectSubtypes(ANARIDataType objectType)
{
  return helide_gpu::query_object_types(objectType);
}

const void *HelideGPUDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  return helide_gpu::query_object_info(
      objectType, objectSubtype, infoName, infoType);
}

const void *HelideGPUDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  return helide_gpu::query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
}

// Other HelideGPUDevice definitions
// /////////////////////////////////////////////

HelideGPUDevice::HelideGPUDevice(ANARIStatusCallback cb, const void *ptr)
    : helium::BaseDevice(cb, ptr)
{
  m_state = std::make_unique<HelideGPUDeviceGlobalState>(this_device());
  deviceCommitParameters();
}

HelideGPUDevice::HelideGPUDevice(ANARILibrary l) : helium::BaseDevice(l)
{
  m_state = std::make_unique<HelideGPUDeviceGlobalState>(this_device());
  deviceCommitParameters();
}

HelideGPUDevice::~HelideGPUDevice()
{
  auto state = deviceState();
  state->commitBuffer.clear();
  reportMessage(ANARI_SEVERITY_DEBUG, "destroying HelideGPU device (%p)", this);
  if (m_initialized)
    state->gpu.thread.enqueue(gpu_device_release, state).wait();
}

void HelideGPUDevice::initDevice()
{
  if (m_initialized)
    return;

  reportMessage(
      ANARI_SEVERITY_DEBUG, "initializing HelideGPU device (%p)", this);

  auto &state = *deviceState();
  if (!state.gpu.sdlDevice.ensureVideoSubsystemInitialized()) {
    reportMessage(ANARI_SEVERITY_FATAL_ERROR,
        "[SDL3_gpu] SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s",
        SDL_GetError());
    return;
  }

  state.gpu.thread.enqueue(gpu_device_init, deviceState()).wait();

  m_initialized = state.gpu.device != nullptr;
}

void HelideGPUDevice::deviceCommitParameters()
{
  helium::BaseDevice::deviceCommitParameters();
}

int HelideGPUDevice::deviceGetProperty(const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t flags)
{
  std::string_view prop = name;
  if (prop == "extension" && type == ANARI_STRING_LIST) {
    helium::writeToVoidP(mem, deviceState()->gpu.extensions.data());
    return 1;
  } else if (prop == "HelideGPU" && type == ANARI_BOOL) {
    helium::writeToVoidP(mem, true);
    return 1;
  }
  return 0;
}

HelideGPUDeviceGlobalState *HelideGPUDevice::deviceState() const
{
  return (HelideGPUDeviceGlobalState *)helium::BaseDevice::m_state.get();
}

} // namespace helide_gpu
