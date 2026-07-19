// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "GPUTexture.h"
// std
#include <cstring>

namespace helide_gpu {

GPUTexture::GPUTexture(SDL_GPUDevice *dev,
    SDL_GPUTextureFormat format,
    SDL_GPUTextureUsageFlags usage,
    SDL_GPUTextureType type)
    : m_device(dev), m_format(format), m_usage(usage), m_type(type)
{}

GPUTexture::~GPUTexture()
{
  free();
}

GPUTexture::GPUTexture(GPUTexture &&other)
    : m_device(other.m_device),
      m_texture(other.m_texture),
      m_format(other.m_format),
      m_usage(other.m_usage),
      m_type(other.m_type),
      m_width(other.m_width),
      m_height(other.m_height),
      m_depth(other.m_depth),
      m_sizeInBytes(other.m_sizeInBytes),
      m_downloadBuffer(other.m_downloadBuffer),
      m_downloadBufferSize(other.m_downloadBufferSize),
      m_lastUploaded(other.m_lastUploaded)
{
  other.m_device = nullptr;
  other.m_texture = nullptr;
  other.m_width = 0;
  other.m_height = 0;
  other.m_depth = 0;
  other.m_sizeInBytes = 0;
  other.m_downloadBuffer = nullptr;
  other.m_downloadBufferSize = 0;
  other.m_lastUploaded = 0;
}

GPUTexture &GPUTexture::operator=(GPUTexture &&other)
{
  if (this != &other) {
    free();
    m_device = other.m_device;
    m_texture = other.m_texture;
    m_format = other.m_format;
    m_usage = other.m_usage;
    m_type = other.m_type;
    m_width = other.m_width;
    m_height = other.m_height;
    m_depth = other.m_depth;
    m_sizeInBytes = other.m_sizeInBytes;
    m_downloadBuffer = other.m_downloadBuffer;
    m_downloadBufferSize = other.m_downloadBufferSize;
    m_lastUploaded = other.m_lastUploaded;

    other.m_device = nullptr;
    other.m_texture = nullptr;
    other.m_width = 0;
    other.m_height = 0;
    other.m_depth = 0;
    other.m_sizeInBytes = 0;
    other.m_downloadBuffer = nullptr;
    other.m_downloadBufferSize = 0;
    other.m_lastUploaded = 0;
  }
  return *this;
}

bool GPUTexture::alloc(uint32_t w, uint32_t h, uint32_t d)
{
  if (m_texture && m_width == w && m_height == h && m_depth == d)
    return true;
  if (!m_device)
    return false;

  free();

  SDL_GPUTextureCreateInfo texInfo{};
  texInfo.type = m_type;
  texInfo.format = m_format;
  texInfo.usage = m_usage;
  texInfo.width = w;
  texInfo.height = h;
  texInfo.layer_count_or_depth = d;
  texInfo.num_levels = 1;
  texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
  m_texture = SDL_CreateGPUTexture(m_device, &texInfo);
  if (!m_texture)
    return false;

  m_width = w;
  m_height = h;
  m_depth = d;
  m_sizeInBytes = w * h * d * formatBytesPerPixel(m_format);
  return true;
}

SDL_GPUTexture *GPUTexture::sdlTexture() const
{
  return m_texture;
}

uint32_t GPUTexture::width() const
{
  return m_width;
}

uint32_t GPUTexture::height() const
{
  return m_height;
}

uint32_t GPUTexture::depth() const
{
  return m_depth;
}

helium::TimeStamp GPUTexture::lastUploaded() const
{
  return m_lastUploaded;
}

bool GPUTexture::valid() const
{
  return m_texture != nullptr;
}

GPUTexture::operator bool() const
{
  return valid();
}

bool GPUTexture::allocDownloadBuffer()
{
  if (m_downloadBuffer && m_downloadBufferSize == m_sizeInBytes)
    return true;
  if (!m_device || m_sizeInBytes == 0)
    return false;

  if (m_downloadBuffer) {
    SDL_ReleaseGPUTransferBuffer(m_device, m_downloadBuffer);
    m_downloadBuffer = nullptr;
    m_downloadBufferSize = 0;
  }

  SDL_GPUTransferBufferCreateInfo tbInfo{};
  tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  tbInfo.size = m_sizeInBytes;
  m_downloadBuffer = SDL_CreateGPUTransferBuffer(m_device, &tbInfo);
  if (!m_downloadBuffer)
    return false;

  m_downloadBufferSize = m_sizeInBytes;
  return true;
}

bool GPUTexture::uploadImpl(
    const void *src, uint32_t sizeInBytes, uint32_t w, uint32_t h, uint32_t d)
{
  if (!alloc(w, h, d))
    return false;

  SDL_GPUTransferBufferCreateInfo tbInfo{};
  tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tbInfo.size = sizeInBytes;
  auto *tb = SDL_CreateGPUTransferBuffer(m_device, &tbInfo);
  if (!tb)
    return false;

  void *mapped = SDL_MapGPUTransferBuffer(m_device, tb, false);
  if (mapped) {
    std::memcpy(mapped, src, sizeInBytes);
    SDL_UnmapGPUTransferBuffer(m_device, tb);
  }

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(m_device);
  SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);

  SDL_GPUTextureTransferInfo srcInfo{};
  srcInfo.transfer_buffer = tb;
  srcInfo.offset = 0;

  SDL_GPUTextureRegion dstRegion{};
  dstRegion.texture = m_texture;
  dstRegion.w = w;
  dstRegion.h = h;
  dstRegion.d = d;

  SDL_UploadToGPUTexture(pass, &srcInfo, &dstRegion, false);
  SDL_EndGPUCopyPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_WaitForGPUIdle(m_device);
  SDL_ReleaseGPUTransferBuffer(m_device, tb);
  m_lastUploaded = helium::newTimeStamp();
  return true;
}

bool GPUTexture::enqueueDownload(SDL_GPUCopyPass *pass)
{
  if (!valid() || !pass)
    return false;
  if (!allocDownloadBuffer())
    return false;

  SDL_GPUTextureRegion src{};
  src.texture = m_texture;
  src.w = m_width;
  src.h = m_height;
  src.d = m_depth;

  SDL_GPUTextureTransferInfo dst{};
  dst.transfer_buffer = m_downloadBuffer;
  dst.offset = 0;

  SDL_DownloadFromGPUTexture(pass, &src, &dst);
  return true;
}

void *GPUTexture::mapDownload()
{
  if (!m_device || !m_downloadBuffer)
    return nullptr;
  return SDL_MapGPUTransferBuffer(m_device, m_downloadBuffer, false);
}

void GPUTexture::unmapDownload()
{
  if (!m_device || !m_downloadBuffer)
    return;
  SDL_UnmapGPUTransferBuffer(m_device, m_downloadBuffer);
}

bool GPUTexture::downloadImpl(void *dst, uint32_t sizeInBytes)
{
  if (!valid() || sizeInBytes > m_sizeInBytes)
    return false;

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(m_device);
  SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
  if (!enqueueDownload(pass)) {
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    return false;
  }
  SDL_EndGPUCopyPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_WaitForGPUIdle(m_device);

  void *mapped = mapDownload();
  if (mapped) {
    std::memcpy(dst, mapped, sizeInBytes);
    unmapDownload();
  }
  return mapped != nullptr;
}

void GPUTexture::free()
{
  if (!m_device)
    return;
  if (m_downloadBuffer) {
    SDL_ReleaseGPUTransferBuffer(m_device, m_downloadBuffer);
    m_downloadBuffer = nullptr;
    m_downloadBufferSize = 0;
  }
  if (m_texture) {
    SDL_ReleaseGPUTexture(m_device, m_texture);
    m_texture = nullptr;
  }
  m_width = 0;
  m_height = 0;
  m_depth = 0;
  m_sizeInBytes = 0;
  m_lastUploaded = 0;
}

uint32_t GPUTexture::formatBytesPerPixel(SDL_GPUTextureFormat format)
{
  switch (format) {
  case SDL_GPU_TEXTUREFORMAT_R8_UNORM:
    return 1;
  case SDL_GPU_TEXTUREFORMAT_R8G8_UNORM:
    return 2;
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
  case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
  case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
  case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT:
    return 4;
  case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT:
    return 8;
  case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT:
    return 16;
  default:
    return 4;
  }
}

} // namespace helide_gpu
