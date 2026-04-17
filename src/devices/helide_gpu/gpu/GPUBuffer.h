// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// sdl_gpu
#include <SDL3/SDL_gpu.h>
// helium
#include "helium/array/Array.h"
#include "helium/utility/TimeStamp.h"
// std
#include <type_traits>

namespace helide_gpu {

struct GPUBuffer
{
  GPUBuffer() = default;
  GPUBuffer(SDL_GPUDevice *dev, SDL_GPUBufferUsageFlags usage);
  ~GPUBuffer();

  GPUBuffer(const GPUBuffer &) = delete;
  GPUBuffer &operator=(const GPUBuffer &) = delete;
  GPUBuffer(GPUBuffer &&);
  GPUBuffer &operator=(GPUBuffer &&);

  template <typename T>
  bool upload(const T *src, size_t numElements);

  bool uploadArray(const helium::Array *arr, bool convertToFloat = false);
  bool allocBytes(uint32_t sizeInBytes);

  template <typename T>
  bool download(T *dst, size_t numElements);

  SDL_GPUBuffer *sdlBuffer() const;
  uint32_t size() const;

  helium::TimeStamp lastUploaded() const;

  bool valid() const;
  operator bool() const;

 private:
  bool alloc(uint32_t sizeInBytes);
  bool uploadImpl(const void *src, uint32_t sizeInBytes);
  bool downloadImpl(void *dst, uint32_t sizeInBytes);
  void free();

  // Data //

  SDL_GPUDevice *m_device{nullptr};
  SDL_GPUBuffer *m_buffer{nullptr};
  SDL_GPUBufferUsageFlags m_usage{};
  uint32_t m_size{0};
  helium::TimeStamp m_lastUploaded{0};
  helium::Array *m_lastUploadArray{nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline bool GPUBuffer::upload(const T *src, size_t numElements)
{
  static_assert(std::is_trivially_copyable<T>::value);
  if (numElements == 0)
    return true;
  const uint32_t sizeInBytes = static_cast<uint32_t>(numElements * sizeof(T));
  return uploadImpl(src, sizeInBytes);
}

template <typename T>
inline bool GPUBuffer::download(T *dst, size_t numElements)
{
  static_assert(std::is_trivially_copyable<T>::value);
  if (numElements == 0)
    return true;
  const uint32_t sizeInBytes = static_cast<uint32_t>(numElements * sizeof(T));
  return downloadImpl(dst, sizeInBytes);
}

} // namespace helide_gpu
