// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace remote {

struct CompressionFeatures
{
  int32_t hasTurboJPEG{false};
  int32_t hasSNAPPY{false};
};

CompressionFeatures getCompressionFeatures();

// ==================================================================
// TurboJPEG
// ==================================================================

struct TurboJPEGOptions
{
  enum class PixelFormat
  {
    RGB,
    BGR,
    RGBX,
    BGRX,
    XBGR,
    XRGB,
    GRAY,
    RGBA,
    BGRA,
    ABGR,
    ARGB,
  };

  int width;
  int height;
  PixelFormat pixelFormat;
  int quality = 80;
};

size_t getMaxCompressedBufferSizeTurboJPEG(TurboJPEGOptions options);

bool compressTurboJPEG(const uint8_t *dataIN,
    uint8_t *dataOUT,
    size_t &compressedSizeInBytesOUT,
    TurboJPEGOptions options);

bool uncompressTurboJPEG(const uint8_t *dataIN,
    uint8_t *dataOUT,
    size_t compressedSizeInBytesIN,
    TurboJPEGOptions options);

// ==================================================================
// Snappy
// ==================================================================

struct SNAPPYOptions
{
  size_t inputSize;
};

size_t getMaxCompressedBufferSizeSNAPPY(SNAPPYOptions);

bool compressSNAPPY(const uint8_t *dataIN,
    uint8_t *dataOUT,
    size_t &compressedSizeInBytesOUT,
    SNAPPYOptions options);

bool uncompressSNAPPY(const uint8_t *dataIN,
    uint8_t *dataOUT,
    size_t compressedSizeInBytesIN,
    SNAPPYOptions options);

} // namespace remote
