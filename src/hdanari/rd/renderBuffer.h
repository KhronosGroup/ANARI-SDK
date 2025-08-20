// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anariTypes.h"
// pxr
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariRenderBuffer : public HdRenderBuffer
{
  HdAnariRenderBuffer(SdfPath const &id);
  ~HdAnariRenderBuffer() override = default;

  bool Allocate(
      const GfVec3i &dimensions, HdFormat format, bool multiSampled) override;

  unsigned int GetWidth() const override;
  unsigned int GetHeight() const override;
  unsigned int GetDepth() const override;
  HdFormat GetFormat() const override;

  bool IsMultiSampled() const override;
  void Resolve() override;

  void *Map() override;
  void Unmap() override;
  bool IsMapped() const override;

  bool IsConverged() const override;
  void SetConverged(bool cv);

  void CopyFromAnariFrame(anari::Device d,
      anari::Frame f,
      const TfToken &aovName,
      const VtValue &clearValue);

  void Clear(const VtValue &clearValue);

 private:
  // ---------------------------------------------------------------------- //
  /// \name I/O helpers
  // ---------------------------------------------------------------------- //
  void Write(const GfVec3i &pixel, size_t numComponents, float const *value);
  void Write(const GfVec3i &pixel, size_t numComponents, int const *value);
  void Clear(size_t numComponents, float const *value);
  void Clear(size_t numComponents, int const *value);

  void _Deallocate() override;

  unsigned int _width{0};
  unsigned int _height{0};
  HdFormat _format{HdFormatInvalid};

  std::vector<uint8_t> _buffer;

  std::atomic<int> _mappers{0};
  std::atomic<bool> _converged{false};
};

PXR_NAMESPACE_CLOSE_SCOPE
