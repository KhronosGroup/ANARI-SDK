// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Compression.h"
#ifdef HAVE_SNAPPY
#include <snappy.h>
#endif
#ifdef HAVE_TURBOJPEG
#include <turbojpeg.h>
#endif
#include <map>
#include "Logging.h"

namespace remote {

// ==================================================================
// TurboJPEG
// ==================================================================

#ifdef HAVE_TURBOJPEG

static std::map<TurboJPEGOptions::PixelFormat, TJPF> MapPixelFormatTurboJPEG = {
    {TurboJPEGOptions::PixelFormat::RGB, TJPF_RGB},
    {TurboJPEGOptions::PixelFormat::BGR, TJPF_BGR},
    {TurboJPEGOptions::PixelFormat::RGBX, TJPF_RGBX},
    {TurboJPEGOptions::PixelFormat::BGRX, TJPF_BGRX},
    {TurboJPEGOptions::PixelFormat::XBGR, TJPF_XBGR},
    {TurboJPEGOptions::PixelFormat::XRGB, TJPF_XRGB},
    {TurboJPEGOptions::PixelFormat::GRAY, TJPF_GRAY},
    {TurboJPEGOptions::PixelFormat::RGBA, TJPF_RGBA},
    {TurboJPEGOptions::PixelFormat::BGRA, TJPF_BGRA},
    {TurboJPEGOptions::PixelFormat::ABGR, TJPF_ABGR},
    {TurboJPEGOptions::PixelFormat::ARGB, TJPF_ARGB},
};

size_t getMaxCompressedBufferSizeTurboJPEG(TurboJPEGOptions options)
{
  return tjBufSize(options.width, options.height, TJSAMP_444);
}

bool compressTurboJPEG(const uint8_t *dataIN,
    uint8_t *dataOUT,
    size_t &compressedSizeInBytesOUT,
    TurboJPEGOptions options)
{
  uint8_t *compressedImage = nullptr;
  unsigned long jpegSize = 0;
  tjhandle jpegCompressor = tjInitCompress();
  if (jpegCompressor == nullptr) {
    LOG(logging::Level::Warning) << "turbojpeg error: " << tjGetErrorStr();
    return false;
  }

  TJPF pixelFormat = MapPixelFormatTurboJPEG[options.pixelFormat];

  int tj_err = 0;
  tj_err = tjCompress2(jpegCompressor,
      dataIN,
      options.width,
      0,
      options.height,
      pixelFormat,
      &compressedImage,
      &jpegSize,
      TJSAMP_444,
      options.quality,
      TJFLAG_FASTDCT);
  if (tj_err != 0) {
    LOG(logging::Level::Warning) << "turbojpeg error: " << tjGetErrorStr();
    return false;
  }

  tjDestroy(jpegCompressor);

  memcpy(dataOUT, compressedImage, jpegSize);
  compressedSizeInBytesOUT = jpegSize;

  tjFree(compressedImage);

  return true;
}

bool uncompressTurboJPEG(const uint8_t *dataIN,
    uint8_t *dataOUT,
    size_t compressedSizeInBytesIN,
    TurboJPEGOptions options)
{
  tjhandle jpegDecompressor = tjInitDecompress();
  if (jpegDecompressor == nullptr)
    LOG(logging::Level::Warning) << "turbojpeg error: " << tjGetErrorStr();

  uint32_t jpegSize(compressedSizeInBytesIN);
  int jpegWidth, jpegHeight, jpegSubsamp;

  int tj_err = 0;
  tj_err = tjDecompressHeader2(jpegDecompressor,
      (uint8_t *)dataIN,
      jpegSize,
      &jpegWidth,
      &jpegHeight,
      &jpegSubsamp);

  if (tj_err != 0) {
    LOG(logging::Level::Warning) << "turbojpeg error: " << tjGetErrorStr();
    return false;
  }

  if (jpegWidth != options.width || jpegHeight != options.height) {
    LOG(logging::Level::Warning)
        << "turbojpeg size mismatch. JPEG size: " << jpegWidth << ','
        << jpegHeight << ", expected: " << options.width << ','
        << options.height;
    return false;
  }

  TJPF pixelFormat = MapPixelFormatTurboJPEG[options.pixelFormat];
  tj_err = tjDecompress2(jpegDecompressor,
      (uint8_t *)dataIN,
      jpegSize,
      dataOUT,
      options.width,
      0,
      options.height,
      pixelFormat,
      TJFLAG_FASTDCT);

  if (tj_err != 0) {
    LOG(logging::Level::Warning) << "turbojpeg error: " << tjGetErrorStr();
  }

  tjDestroy(jpegDecompressor);

  return true;
}

#endif

// ==================================================================
// Snappy
// ==================================================================

#ifdef HAVE_SNAPPY

size_t getMaxCompressedBufferSizeSNAPPY(SNAPPYOptions options)
{
  return snappy::MaxCompressedLength(options.inputSize);
}

bool compressSNAPPY(const uint8_t *dataIN,
    uint8_t *dataOUT,
    size_t &compressedSizeInBytesOUT,
    SNAPPYOptions options)
{
  snappy::RawCompress((const char *)dataIN,
      options.inputSize,
      (char *)dataOUT,
      &compressedSizeInBytesOUT);

  return true;
}

bool uncompressSNAPPY(const uint8_t *dataIN,
    uint8_t *dataOUT,
    size_t compressedSizeInBytesIN,
    SNAPPYOptions options)
{
  (void)options;
  return snappy::RawUncompress(
      (const char *)dataIN, compressedSizeInBytesIN, (char *)dataOUT);
}

#endif

} // namespace remote
