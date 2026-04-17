// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// sdl_gpu
#include <SDL3/SDL_gpu.h>
// helium
#include "helium/utility/TimeStamp.h"
// std
#include <type_traits>

namespace helide_gpu {

struct GPUTexture
{
  GPUTexture() = default;
  GPUTexture(SDL_GPUDevice *dev,
      SDL_GPUTextureFormat format,
      SDL_GPUTextureUsageFlags usage,
      SDL_GPUTextureType type = SDL_GPU_TEXTURETYPE_2D);
  ~GPUTexture();

  GPUTexture(const GPUTexture &) = delete;
  GPUTexture &operator=(const GPUTexture &) = delete;
  GPUTexture(GPUTexture &&);
  GPUTexture &operator=(GPUTexture &&);

  // Allocate (or reallocate if dimensions changed)
  bool alloc(uint32_t w, uint32_t h = 1, uint32_t d = 1);

  // Upload CPU data to GPU texture (calls alloc internally)
  template <typename T>
  bool upload(const T *src,
      size_t numElements,
      uint32_t w,
      uint32_t h = 1,
      uint32_t d = 1);

  // Standalone download GPU data to CPU (own copy pass + wait)
  template <typename T>
  bool download(T *dst, size_t numElements);

  // Batched download: enqueue into an existing copy pass, then map/unmap later
  bool enqueueDownload(SDL_GPUCopyPass *pass);
  void *mapDownload();
  void unmapDownload();

  SDL_GPUTexture *sdlTexture() const;
  uint32_t width() const;
  uint32_t height() const;
  uint32_t depth() const;

  helium::TimeStamp lastUploaded() const;

  bool valid() const;
  operator bool() const;

 private:
  bool allocDownloadBuffer();
  bool uploadImpl(const void *src,
      uint32_t sizeInBytes,
      uint32_t w,
      uint32_t h,
      uint32_t d);
  bool downloadImpl(void *dst, uint32_t sizeInBytes);
  void free();

  static uint32_t formatBytesPerPixel(SDL_GPUTextureFormat format);

  // Data //

  SDL_GPUDevice *m_device{nullptr};
  SDL_GPUTexture *m_texture{nullptr};
  SDL_GPUTextureFormat m_format{};
  SDL_GPUTextureUsageFlags m_usage{};
  SDL_GPUTextureType m_type{SDL_GPU_TEXTURETYPE_2D};
  uint32_t m_width{0};
  uint32_t m_height{0};
  uint32_t m_depth{0};
  uint32_t m_sizeInBytes{0};

  SDL_GPUTransferBuffer *m_downloadBuffer{nullptr};
  uint32_t m_downloadBufferSize{0};

  helium::TimeStamp m_lastUploaded{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline bool GPUTexture::upload(
    const T *src, size_t numElements, uint32_t w, uint32_t h, uint32_t d)
{
  static_assert(std::is_trivially_copyable<T>::value);
  if (numElements == 0)
    return true;
  const uint32_t sizeInBytes = static_cast<uint32_t>(numElements * sizeof(T));
  return uploadImpl(src, sizeInBytes, w, h, d);
}

template <typename T>
inline bool GPUTexture::download(T *dst, size_t numElements)
{
  static_assert(std::is_trivially_copyable<T>::value);
  if (numElements == 0)
    return true;
  const uint32_t sizeInBytes = static_cast<uint32_t>(numElements * sizeof(T));
  return downloadImpl(dst, sizeInBytes);
}

} // namespace helide_gpu
