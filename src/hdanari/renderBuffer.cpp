// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "renderBuffer.h"
#include "renderParam.h"
// pxr
#include <pxr/base/gf/half.h>
#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

// Helper functions ///////////////////////////////////////////////////////////

static GfVec4f _GetClearColor(VtValue const &clearValue)
{
  HdTupleType type = HdGetValueTupleType(clearValue);
  if (type.count != 1) {
    return GfVec4f(0.0f, 0.0f, 0.0f, 1.0f);
  }

  switch (type.type) {
  case HdTypeFloatVec3: {
    GfVec3f f = *(static_cast<const GfVec3f *>(HdGetValueData(clearValue)));
    return GfVec4f(f[0], f[1], f[2], 1.0f);
  }
  case HdTypeFloatVec4: {
    GfVec4f f = *(static_cast<const GfVec4f *>(HdGetValueData(clearValue)));
    return f;
  }
  case HdTypeDoubleVec3: {
    GfVec3d f = *(static_cast<const GfVec3d *>(HdGetValueData(clearValue)));
    return GfVec4f(f[0], f[1], f[2], 1.0f);
  }
  case HdTypeDoubleVec4: {
    GfVec4d f = *(static_cast<const GfVec4d *>(HdGetValueData(clearValue)));
    return GfVec4f(f);
  }
  default:
    return GfVec4f(0.0f, 0.0f, 0.0f, 1.0f);
  }
}

template <typename T>
static void _WriteSample(
    HdFormat format, uint8_t *dst, size_t valueComponents, T const *value)
{
  HdFormat componentFormat = HdGetComponentFormat(format);
  size_t componentCount = HdGetComponentCount(format);

  for (size_t c = 0; c < componentCount; ++c) {
    if (componentFormat == HdFormatInt32) {
      ((int32_t *)dst)[c] += (c < valueComponents) ? (int32_t)(value[c]) : 0;
    } else {
      ((float *)dst)[c] += (c < valueComponents) ? (float)(value[c]) : 0.0f;
    }
  }
}

template <typename T>
static void _WriteOutput(
    HdFormat format, uint8_t *dst, size_t valueComponents, T const *value)
{
  HdFormat componentFormat = HdGetComponentFormat(format);
  size_t componentCount = HdGetComponentCount(format);

  for (size_t c = 0; c < componentCount; ++c) {
    if (componentFormat == HdFormatInt32) {
      ((int32_t *)dst)[c] = (c < valueComponents) ? (int32_t)(value[c]) : 0;
    } else if (componentFormat == HdFormatFloat16) {
      ((uint16_t *)dst)[c] =
          (c < valueComponents) ? GfHalf(value[c]).bits() : 0;
    } else if (componentFormat == HdFormatFloat32) {
      ((float *)dst)[c] = (c < valueComponents) ? (float)(value[c]) : 0.0f;
    } else if (componentFormat == HdFormatUNorm8) {
      ((uint8_t *)dst)[c] =
          (c < valueComponents) ? (uint8_t)(value[c] * 255.0f) : 0.0f;
    } else if (componentFormat == HdFormatSNorm8) {
      ((int8_t *)dst)[c] =
          (c < valueComponents) ? (int8_t)(value[c] * 127.0f) : 0.0f;
    }
  }
}

static size_t _GetBufferSize(GfVec2i const &dims, HdFormat format)
{
  return dims[0] * dims[1] * HdDataSizeOfFormat(format);
}

static HdFormat _GetSampleFormat(HdFormat format)
{
  HdFormat component = HdGetComponentFormat(format);
  size_t arity = HdGetComponentCount(format);

  if (component == HdFormatUNorm8 || component == HdFormatSNorm8
      || component == HdFormatFloat16 || component == HdFormatFloat32) {
    if (arity == 1) {
      return HdFormatFloat32;
    } else if (arity == 2) {
      return HdFormatFloat32Vec2;
    } else if (arity == 3) {
      return HdFormatFloat32Vec3;
    } else if (arity == 4) {
      return HdFormatFloat32Vec4;
    }
  } else if (component == HdFormatInt32) {
    if (arity == 1) {
      return HdFormatInt32;
    } else if (arity == 2) {
      return HdFormatInt32Vec2;
    } else if (arity == 3) {
      return HdFormatInt32Vec3;
    } else if (arity == 4) {
      return HdFormatInt32Vec4;
    }
  }
  return HdFormatInvalid;
}

// HdAnariRenderBuffer definitions ////////////////////////////////////////////

HdAnariRenderBuffer::HdAnariRenderBuffer(SdfPath const &id) : HdRenderBuffer(id)
{}

bool HdAnariRenderBuffer::Allocate(
    GfVec3i const &dimensions, HdFormat format, bool /*multiSampled*/)
{
  _Deallocate();

  if (dimensions[2] != 1) {
    TF_WARN(
        "Render buffer allocated with dims <%d, %d, %d> and"
        " format %s; depth must be 1! Treating depth as 1 anyway.",
        dimensions[0],
        dimensions[1],
        dimensions[2],
        TfEnum::GetName(format).c_str());
    return false;
  }

  _width = dimensions[0];
  _height = dimensions[1];
  _format = format;
  _buffer.resize(_GetBufferSize(GfVec2i(_width, _height), format));

  return true;
}

unsigned int HdAnariRenderBuffer::GetWidth() const
{
  return _width;
}

unsigned int HdAnariRenderBuffer::GetHeight() const
{
  return _height;
}

unsigned int HdAnariRenderBuffer::GetDepth() const
{
  return 1;
}

HdFormat HdAnariRenderBuffer::GetFormat() const
{
  return _format;
}

bool HdAnariRenderBuffer::IsMultiSampled() const
{
  return false;
}

void HdAnariRenderBuffer::Resolve()
{
  // no-op (not multisampled)
}

void *HdAnariRenderBuffer::Map()
{
  _mappers++;
  return _buffer.data();
}

void HdAnariRenderBuffer::Unmap()
{
  _mappers--;
}

bool HdAnariRenderBuffer::IsMapped() const
{
  return _mappers.load() != 0;
}

bool HdAnariRenderBuffer::IsConverged() const
{
  return _converged.load();
}

void HdAnariRenderBuffer::SetConverged(bool cv)
{
  _converged.store(cv);
}

void HdAnariRenderBuffer::CopyFromAnariFrame(anari::Device d,
    anari::Frame f,
    const TfToken &aovName,
    const VtValue &clearValue)
{
  auto setData = [&](const char *channel, bool checkPixelType) -> bool {
    auto fb = anari::map<void>(d, f, channel);

    if (fb.pixelType == ANARI_UNKNOWN) {
      TF_WARN("Anari frame '%s' buffer is not present -- skipping", channel);
      anari::unmap(d, f, channel);
      return false;
    }

    const uint32_t width = GetWidth();
    const uint32_t height = GetHeight();
    if (fb.width != width || fb.height != height) {
      TF_WARN(
          "AOV buffer and Anari frame size disagree -- skipping '%s'", channel);
      anari::unmap(d, f, channel);
      return false;
    }

    const size_t pixelBytes = HdDataSizeOfFormat(_format);
    if (checkPixelType) {
      if (pixelBytes != anari::sizeOf(fb.pixelType)) {
        TF_WARN("AOV buffer and Anari frame format disagree -- skipping '%s'",
            channel);
        anari::unmap(d, f, channel);
        return false;
      }
    }

    const uint32_t nBytes = width * height * pixelBytes;
    std::memcpy(_buffer.data(), fb.data, nBytes);
    anari::unmap(d, f, channel);
    return true;
  };

  bool dataWritten = false;

  if (aovName == HdAovTokens->color)
    dataWritten = setData("channel.color", true);
  else if (aovName == HdAovTokens->cameraDepth || aovName == HdAovTokens->depth)
    dataWritten = setData("channel.depth", true);
  else if (aovName == HdAovTokens->elementId)
    dataWritten = setData("channel.primitiveId", false);
  else if (aovName == HdAovTokens->primId)
    dataWritten = setData("channel.objectId", false);
  else if (aovName == HdAovTokens->instanceId)
    dataWritten = setData("channel.instanceId", false);
  else
    TF_WARN("unable to copy AOV buffer '%s'", aovName.GetText());

  if (!dataWritten)
    Clear(clearValue);
}

void HdAnariRenderBuffer::Clear(const VtValue &clearValue)
{
  if (GetFormat() == HdFormatInt32) {
    int32_t v = clearValue.Get<int32_t>();
    Clear(1, &v);
  } else if (GetFormat() == HdFormatFloat32) {
    float v = clearValue.Get<float>();
    Clear(1, &v);
  } else if (GetFormat() == HdFormatFloat32Vec3) {
    GfVec3f v = clearValue.Get<GfVec3f>();
    Clear(3, v.data());
  } else {
    GfVec4f clearColor = _GetClearColor(clearValue);
    Clear(4, clearColor.data());
  }
}

void HdAnariRenderBuffer::Write(
    GfVec3i const &pixel, size_t numComponents, float const *value)
{
  size_t idx = pixel[1] * _width + pixel[0];
  size_t formatSize = HdDataSizeOfFormat(_format);
  uint8_t *dst = &_buffer[idx * formatSize];
  _WriteOutput(_format, dst, numComponents, value);
}

void HdAnariRenderBuffer::Write(
    GfVec3i const &pixel, size_t numComponents, int const *value)
{
  size_t idx = pixel[1] * _width + pixel[0];
  size_t formatSize = HdDataSizeOfFormat(_format);
  uint8_t *dst = &_buffer[idx * formatSize];
  _WriteOutput(_format, dst, numComponents, value);
}

void HdAnariRenderBuffer::Clear(size_t numComponents, float const *value)
{
  size_t formatSize = HdDataSizeOfFormat(_format);
  for (size_t i = 0; i < _width * _height; ++i) {
    uint8_t *dst = &_buffer[i * formatSize];
    _WriteOutput(_format, dst, numComponents, value);
  }
}

void HdAnariRenderBuffer::Clear(size_t numComponents, int const *value)
{
  size_t formatSize = HdDataSizeOfFormat(_format);
  for (size_t i = 0; i < _width * _height; ++i) {
    uint8_t *dst = &_buffer[i * formatSize];
    _WriteOutput(_format, dst, numComponents, value);
  }
}

void HdAnariRenderBuffer::_Deallocate()
{
  // If the buffer is mapped while we're doing this, there's not a great
  // recovery path...
  TF_VERIFY(!IsMapped());
}

PXR_NAMESPACE_CLOSE_SCOPE
