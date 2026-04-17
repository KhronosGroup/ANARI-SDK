// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "GPUBuffer.h"
#include "HelideGPUColorSpace.h"
// anari
#include <anari/frontend/type_utility.h>
// std
#include <cstring>
#include <vector>

namespace helide_gpu {

static bool isDirectFloat(ANARIDataType t)
{
  return t == ANARI_FLOAT32 || t == ANARI_FLOAT32_VEC2
      || t == ANARI_FLOAT32_VEC3 || t == ANARI_FLOAT32_VEC4;
}

GPUBuffer::GPUBuffer(SDL_GPUDevice *dev, SDL_GPUBufferUsageFlags usage)
    : m_device(dev), m_usage(usage)
{}

GPUBuffer::~GPUBuffer()
{
  free();
}

GPUBuffer::GPUBuffer(GPUBuffer &&other)
    : m_device(other.m_device),
      m_buffer(other.m_buffer),
      m_usage(other.m_usage),
      m_size(other.m_size),
      m_lastUploaded(other.m_lastUploaded)
{
  other.m_device = nullptr;
  other.m_buffer = nullptr;
  other.m_size = 0;
  other.m_lastUploaded = 0;
  other.m_lastUploadArray = nullptr;
}

GPUBuffer &GPUBuffer::operator=(GPUBuffer &&other)
{
  if (this != &other) {
    free();
    m_device = other.m_device;
    m_buffer = other.m_buffer;
    m_usage = other.m_usage;
    m_size = other.m_size;
    m_lastUploaded = other.m_lastUploaded;

    other.m_device = nullptr;
    other.m_buffer = nullptr;
    other.m_size = 0;
    other.m_lastUploaded = 0;
    other.m_lastUploadArray = nullptr;
  }
  return *this;
}

bool GPUBuffer::uploadArray(const helium::Array *arr, bool convertToFloat)
{
  if (!arr)
    return false;
  else if (arr == m_lastUploadArray && m_lastUploaded > arr->lastDataModified())
    return true;

  bool success = false;

  if (!convertToFloat || isDirectFloat(arr->elementType())) {
    success = uploadImpl(arr->data(),
        static_cast<uint32_t>(
            arr->totalSize() * anari::sizeOf(arr->elementType())));
  } else {
    const ANARIDataType type = arr->elementType();
    const size_t numElements = arr->totalSize();
    const uint32_t nc = static_cast<uint32_t>(anari::componentsOf(type));
    std::vector<float> converted(numElements * nc);

    if (isElementTypeSRGB(type)) {
      const int srgbNC = srgbComponentCount(type);
      const auto *bytes = static_cast<const uint8_t *>(arr->data());
      for (size_t i = 0; i < numElements; ++i) {
        vec4 v = srgbBytesToLinear(bytes + i * srgbNC, srgbNC);
        for (uint32_t c = 0; c < nc; ++c)
          converted[i * nc + c] = v[c];
      }
    } else {
      for (size_t i = 0; i < numElements; ++i) {
        auto v = arr->readAsAttributeValue(static_cast<int32_t>(i));
        float tmp[4];
        std::memcpy(tmp, &v, sizeof(tmp));
        for (uint32_t c = 0; c < nc; ++c)
          converted[i * nc + c] = tmp[c];
      }
    }

    success = uploadImpl(converted.data(),
        static_cast<uint32_t>(converted.size() * sizeof(float)));
  }

  if (success)
    m_lastUploadArray = const_cast<helium::Array *>(arr);

  return success;
}

bool GPUBuffer::allocBytes(uint32_t sizeInBytes)
{
  return alloc(sizeInBytes);
}

SDL_GPUBuffer *GPUBuffer::sdlBuffer() const
{
  return m_buffer;
}

uint32_t GPUBuffer::size() const
{
  return m_size;
}

helium::TimeStamp GPUBuffer::lastUploaded() const
{
  return m_lastUploaded;
}

bool GPUBuffer::valid() const
{
  return sdlBuffer() != nullptr;
}

GPUBuffer::operator bool() const
{
  return valid();
}

bool GPUBuffer::alloc(uint32_t sizeInBytes)
{
  if (m_buffer && m_size == sizeInBytes)
    return true;
  else if (!m_device)
    return false;
  free();
  SDL_GPUBufferCreateInfo bufInfo{};
  bufInfo.usage = m_usage;
  bufInfo.size = sizeInBytes;
  m_buffer = SDL_CreateGPUBuffer(m_device, &bufInfo);
  m_size = m_buffer ? sizeInBytes : 0;
  return m_buffer != nullptr;
}

bool GPUBuffer::uploadImpl(const void *mem, uint32_t sizeInBytes)
{
  if (!alloc(sizeInBytes))
    return false;

  SDL_GPUTransferBufferCreateInfo tbInfo{};
  tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tbInfo.size = sizeInBytes;
  auto *tb = SDL_CreateGPUTransferBuffer(m_device, &tbInfo);
  if (!tb)
    return false;

  void *mapped = SDL_MapGPUTransferBuffer(m_device, tb, false);
  if (mapped) {
    memcpy(mapped, mem, sizeInBytes);
    SDL_UnmapGPUTransferBuffer(m_device, tb);
  }

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(m_device);
  SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);

  SDL_GPUTransferBufferLocation src{};
  src.transfer_buffer = tb;
  src.offset = 0;

  SDL_GPUBufferRegion dst{};
  dst.buffer = m_buffer;
  dst.offset = 0;
  dst.size = m_size;

  SDL_UploadToGPUBuffer(pass, &src, &dst, false);
  SDL_EndGPUCopyPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_WaitForGPUIdle(m_device);
  SDL_ReleaseGPUTransferBuffer(m_device, tb);
  m_lastUploaded = helium::newTimeStamp();
  m_lastUploadArray = nullptr;
  return true;
}

bool GPUBuffer::downloadImpl(void *dst, uint32_t sizeInBytes)
{
  if (!valid() || sizeInBytes > m_size)
    return false;
  // TODO
  return false;
}

void GPUBuffer::free()
{
  if (!m_device || !m_buffer)
    return;
  SDL_ReleaseGPUBuffer(m_device, m_buffer);
  m_buffer = nullptr;
  m_size = 0;
  m_lastUploaded = 0;
  m_lastUploadArray = nullptr;
}

} // namespace helide_gpu
