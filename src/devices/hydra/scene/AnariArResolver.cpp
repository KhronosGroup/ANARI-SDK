// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The Ar side of the in-memory asset scheme (SPEC §6.3). Storm never gets here:
// Hio dispatches `.anari` paths to AnariHioImage by extension. Delegates that
// read texture bytes through Ar and decode them with their own image loaders
// (hdGatling) resolve `anari://` through this URI resolver instead, which
// serves each registry image encoded as a file those loaders understand: an
// 8-bit image as its exact bytes in an uncompressed 32-bit TGA, a Half/Float
// image as an uncompressed EXR of the same channel type. An environment (dome)
// image is always an EXR, an 8-bit one widened to linear Float: hdGatling
// stages every dome texture as HDR and crashes on an 8-bit decode. Rows are
// written top first, the order AnariHioImage serves them to Storm.

#include "AnariImage.h"

#include <pxr/usd/ar/defineResolver.h>
#include <pxr/usd/ar/inMemoryAsset.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/writableAsset.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace anari_hydra {
namespace {

void put8(std::vector<uint8_t> &out, uint8_t v)
{
  out.push_back(v);
}

void put16(std::vector<uint8_t> &out, uint16_t v)
{
  out.push_back(uint8_t(v));
  out.push_back(uint8_t(v >> 8));
}

void put32(std::vector<uint8_t> &out, uint32_t v)
{
  for (int i = 0; i < 4; ++i)
    out.push_back(uint8_t(v >> (8 * i)));
}

void put64(std::vector<uint8_t> &out, uint64_t v)
{
  for (int i = 0; i < 8; ++i)
    out.push_back(uint8_t(v >> (8 * i)));
}

void putFloat(std::vector<uint8_t> &out, float f)
{
  uint32_t v;
  std::memcpy(&v, &f, 4);
  put32(out, v);
}

void putString(std::vector<uint8_t> &out, const char *s)
{
  out.insert(out.end(), s, s + std::strlen(s) + 1);
}

// An uncompressed, top-origin 32-bit BGRA TGA of an 8-bit image's raw bytes
// (sRGB codes stay codes, as AnariHioImage serves them).
std::vector<uint8_t> EncodeTga(const AnariImageData &img)
{
  std::vector<uint8_t> out;
  out.reserve(18 + img.texels.size() + 26);
  put8(out, 0); // no image ID
  put8(out, 0); // no colour map
  put8(out, 2); // uncompressed true-colour
  for (int i = 0; i < 5; ++i)
    put8(out, 0); // colour-map spec
  put16(out, 0); // x origin
  put16(out, 0); // y origin
  put16(out, uint16_t(img.width));
  put16(out, uint16_t(img.height));
  put8(out, 32); // bits per pixel
  put8(out, 0x28); // 8 alpha bits, top-left origin
  for (size_t i = 0, n = size_t(img.width) * img.height; i < n; ++i) {
    const uint8_t *p = img.texel(i);
    put8(out, p[2]);
    put8(out, p[1]);
    put8(out, p[0]);
    put8(out, p[3]);
  }
  // TGA 2.0 footer (no extension/developer areas). Besides marking the
  // version, it keeps even a 1x1 file above 33 bytes: hdGatling tries PNG
  // first, and a buffer shorter than PNG's signature + IHDR reads as a corrupt
  // PNG rather than "not a PNG", ending its decoder search before TGA.
  put32(out, 0);
  put32(out, 0);
  putString(out, "TRUEVISION-XFILE.");
  return out;
}

// A single-part, uncompressed scanline EXR with RGBA channels of the image's
// own type (HALF for Half, FLOAT for Float), so the texel bits carry over
// unchanged.
std::vector<uint8_t> EncodeExr(const AnariImageData &img)
{
  const bool half = img.format == TexelFormat::Half;
  const uint32_t pixelType = half ? 1 : 2;
  const size_t chanBytes = half ? 2 : 4;
  const int w = img.width;
  const int h = img.height;

  std::vector<uint8_t> out;
  put32(out, 20000630); // magic
  put32(out, 2); // version 2, single-part scanline

  // Channels are stored in alphabetical order: A, B, G, R.
  putString(out, "channels");
  putString(out, "chlist");
  put32(out, 4 * (2 + 16) + 1);
  for (const char *name : {"A", "B", "G", "R"}) {
    putString(out, name);
    put32(out, pixelType);
    put32(out, 0); // pLinear + reserved
    put32(out, 1); // xSampling
    put32(out, 1); // ySampling
  }
  put8(out, 0);

  putString(out, "compression");
  putString(out, "compression");
  put32(out, 1);
  put8(out, 0); // NO_COMPRESSION

  for (const char *window : {"dataWindow", "displayWindow"}) {
    putString(out, window);
    putString(out, "box2i");
    put32(out, 16);
    put32(out, 0);
    put32(out, 0);
    put32(out, uint32_t(w - 1));
    put32(out, uint32_t(h - 1));
  }

  putString(out, "lineOrder");
  putString(out, "lineOrder");
  put32(out, 1);
  put8(out, 0); // INCREASING_Y

  putString(out, "pixelAspectRatio");
  putString(out, "float");
  put32(out, 4);
  putFloat(out, 1.f);

  putString(out, "screenWindowCenter");
  putString(out, "v2f");
  put32(out, 8);
  putFloat(out, 0.f);
  putFloat(out, 0.f);

  putString(out, "screenWindowWidth");
  putString(out, "float");
  put32(out, 4);
  putFloat(out, 1.f);

  put8(out, 0); // end of header

  // One scanline per block; each block is y, byte count, then the channels'
  // runs of `w` values.
  const size_t lineBytes = size_t(w) * 4 * chanBytes;
  const uint64_t firstBlock = out.size() + size_t(h) * 8;
  for (int y = 0; y < h; ++y)
    put64(out, firstBlock + uint64_t(y) * (8 + lineBytes));

  static const int kChannelOffset[4] = {3, 2, 1, 0}; // A, B, G, R in RGBA
  for (int y = 0; y < h; ++y) {
    put32(out, uint32_t(y));
    put32(out, uint32_t(lineBytes));
    for (int c : kChannelOffset) {
      for (int x = 0; x < w; ++x) {
        const uint8_t *p = img.texel(size_t(y) * w + x) + c * chanBytes;
        out.insert(out.end(), p, p + chanBytes);
      }
    }
  }
  return out;
}

std::vector<uint8_t> EncodeImage(const AnariImageData &img)
{
  const bool eightBit =
      img.format == TexelFormat::UNorm8 || img.format == TexelFormat::Srgb8;
  if (eightBit && !img.environment && img.width <= 0xFFFF
      && img.height <= 0xFFFF)
    return EncodeTga(img);
  if (eightBit) {
    // An environment image, or too large for TGA's 16-bit dimensions: widen
    // to linear Float for the EXR.
    AnariImageData wide;
    wide.allocate(img.width, img.height, TexelFormat::Float);
    for (size_t i = 0, n = size_t(img.width) * img.height; i < n; ++i)
      wide.write(i, img.read(i));
    return EncodeExr(wide);
  }
  return EncodeExr(img);
}

} // namespace
} // namespace anari_hydra

// Must live in the pxr namespace so its TfType name is exactly
// "AnariArResolver", matching the plugInfo Types key (as with AnariHioImage).
PXR_NAMESPACE_OPEN_SCOPE

class AnariArResolver final : public ArResolver
{
 protected:
  std::string _CreateIdentifier(
      const std::string &assetPath, const ArResolvedPath &) const override
  {
    return assetPath;
  }

  std::string _CreateIdentifierForNewAsset(
      const std::string &assetPath, const ArResolvedPath &) const override
  {
    return assetPath;
  }

  ArResolvedPath _Resolve(const std::string &assetPath) const override
  {
    return ArResolvedPath(assetPath);
  }

  ArResolvedPath _ResolveForNewAsset(const std::string &) const override
  {
    return ArResolvedPath();
  }

  std::shared_ptr<ArAsset> _OpenAsset(
      const ArResolvedPath &resolvedPath) const override
  {
    anari_hydra::AnariImageData img;
    if (!anari_hydra::AnariImageRegistry::instance().get(
            resolvedPath.GetPathString(), img)
        || img.width <= 0 || img.height <= 0)
      return nullptr;
    const std::vector<uint8_t> bytes = anari_hydra::EncodeImage(img);
    std::shared_ptr<char> buffer(
        new char[bytes.size()], std::default_delete<char[]>());
    std::memcpy(buffer.get(), bytes.data(), bytes.size());
    return ArInMemoryAsset::FromBuffer(buffer, bytes.size());
  }

  std::shared_ptr<ArWritableAsset> _OpenAssetForWrite(
      const ArResolvedPath &, WriteMode) const override
  {
    return nullptr;
  }
};

AR_DEFINE_RESOLVER(AnariArResolver, ArResolver);

PXR_NAMESPACE_CLOSE_SCOPE
