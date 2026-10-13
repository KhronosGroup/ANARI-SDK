// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The in-memory asset scheme. A process-global registry maps an
// `anari://…​.anari` asset path to decoded image pixels; a self-registered
// HioImage plugin (AnariImage.cpp, compiled into the device library) serves
// those pixels straight to HdStorm's texture pipeline, dispatched purely by the
// `anari` extension — no file on disk, no Ar resolution. Copy-at-load into the
// registry is permitted (outside the zero-copy scope). Proven in
// prototypes/013-inmemory-hio-asset.

#pragma once

// helium
#include "helium/helium_math.h"
// anari
#include <anari/anari.h>
// std
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace helium {
class Array1D;
class Array2D;
} // namespace helium

namespace anari_hydra {

// Texel storage of a decoded image. Each value is served to Storm as the
// matching RGBA HioFormat (UNorm8Vec4 / UNorm8Vec4srgb / Float16Vec4 /
// Float32Vec4), which HdSt uploads without conversion, so the stored bytes are
// the GPU texture. The precision follows the source: an 8-bit linear source
// round-trips exactly through UNorm8; an 8-bit sRGB source keeps its raw bytes
// in Srgb8, an sRGB-typed texture the GPU decodes at sample time (colour
// through the sRGB curve, alpha linear, as helium decodes it), so nothing is
// re-quantised; RA_SRGB, whose linear alpha helium places in green, is
// linearised on decode into Half instead (11-bit mantissa, far finer than the
// 8-bit source); every wider or floating source, and any per-texel transform
// (whose results may leave [0,1]), is stored as Float and is exact.
enum class TexelFormat
{
  UNorm8,
  Srgb8,
  Half,
  Float
};
// Ascending precision: decoders cap with std::min on this order. A UNorm8 cap
// stores an sRGB source linearised to 8 bits.
static_assert(TexelFormat::UNorm8 < TexelFormat::Srgb8
    && TexelFormat::Srgb8 < TexelFormat::Half
    && TexelFormat::Half < TexelFormat::Float);

size_t BytesPerTexel(TexelFormat f);

// The storage precision for an image decoded from `elementType`, `transformed`
// when a TexelAffine is applied to its texels (see the TexelFormat rules).
TexelFormat TexelFormatFor(ANARIDataType elementType, bool transformed);

// Decoded texels for one image, always four channels of RGBA: linear, except
// Srgb8, whose colour channels hold sRGB codes that the GPU (or read())
// decodes. Values are never clamped: HDR and signed texels keep their value in
// the Half/Float formats.
struct AnariImageData
{
  int width = 0;
  int height = 0;
  TexelFormat format = TexelFormat::UNorm8;
  // width*height*BytesPerTexel(format) bytes; row 0 = ANARI image origin.
  std::vector<uint8_t> texels;
  // A dome light's environment image. Storm reads it like any other; the Ar
  // side of the scheme (AnariArResolver.cpp) always encodes it as EXR, since
  // hdGatling's dome loader only handles HDR-decoded images.
  bool environment = false;

  void allocate(int w, int h, TexelFormat f);
  // Store texel `i` (row-major) given in linear RGBA. UNorm8 and Srgb8 clamp
  // to [0,1] by construction (only used for 8-bit sources, whose values
  // already lie there), and Srgb8 encodes colour with the sRGB curve; Half
  // clamps to the finite half range so an out-of-range HDR value cannot
  // become +/-inf and poison a downstream convolution (the dome pipeline).
  void write(size_t i, const anari::math::float4 &c);
  const uint8_t *texel(size_t i) const
  {
    return &texels[i * BytesPerTexel(format)];
  }
  // Texel `i` (row-major) as float RGBA, whatever the storage format.
  anari::math::float4 read(size_t i) const;
};

// Process-global image store keyed by asset path. Both the sampler code and the
// HioImage plugin (same library) touch it directly.
class AnariImageRegistry
{
 public:
  static AnariImageRegistry &instance();

  void set(const std::string &path, AnariImageData data);
  void erase(const std::string &path);
  bool get(const std::string &path, AnariImageData &out) const;

 private:
  mutable std::mutex m_mutex;
  std::map<std::string, AnariImageData> m_images;
};

// An affine map applied to each decoded texel before storage:
// out = matrix * texel + offset. Carries a sampler's channel-mixing
// outTransform/outOffset into its own registry copy of the image. Results are
// stored in Float, so they may leave [0,1].
struct TexelAffine
{
  anari::math::mat4 matrix{linalg::identity};
  anari::math::float4 offset{0.f, 0.f, 0.f, 0.f};
  anari::math::float4 apply(const anari::math::float4 &c) const
  {
    return linalg::mul(matrix, c) + offset;
  }
};

// A one-texel frame of `color` added on both sides of each padded axis, so a
// clamp-to-edge lookup of the padded image reproduces clamp-to-border sampling
// of the source with an arbitrary border colour (the sampler remaps its
// coordinates onto the interior). `transform`, when given to the decoder,
// applies to the border texel as it does to every source texel, since ANARI's
// output map acts on the sampled value. Padded images are stored as Float.
struct TexelBorder
{
  bool padX = false;
  bool padY = false;
  anari::math::float4 color{0.f, 0.f, 0.f, 0.f};
};

// Decode a helium Array2D into linear RGBA via helium's readAsAttributeValue
// (which linearises sRGB source types), at the precision TexelFormatFor
// selects, capped at `maxFormat` (enum order is ascending precision); an Srgb8
// result copies the source bytes instead. Shared by the sampler image path and
// the hdri dome light's environment image. Returns false (leaving `out` empty)
// when `img` is null. `transform`, when given, is applied to each texel in
// float before storage; `border`, when given, frames the padded axes (see
// TexelBorder).
bool DecodeArray2DToImage(helium::Array2D *img,
    AnariImageData &out,
    const TexelAffine *transform = nullptr,
    TexelFormat maxFormat = TexelFormat::Float,
    const TexelBorder *border = nullptr);

// The same for a helium Array1D, stored as an N×1 image. Only the X axis of a
// 1D image can be padded.
bool DecodeArray1DToImage(helium::Array1D *img,
    AnariImageData &out,
    const TexelAffine *transform = nullptr,
    TexelFormat maxFormat = TexelFormat::Float,
    const TexelBorder *border = nullptr);

// Register the Hio plugin exactly once, resolving plugInfo.json relative to the
// device shared library, so no environment setup is needed. Safe to call
// repeatedly.
void EnsureAnariHioRegistered();

} // namespace anari_hydra
