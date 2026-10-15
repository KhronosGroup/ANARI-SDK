// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "AnariImage.h"
// helium
#include "helium/array/Array1D.h"
#include "helium/array/Array2D.h"
#include "helium/helium_math.h"

#include <pxr/base/gf/half.h>
#include <pxr/base/plug/registry.h>
#include <pxr/imaging/hio/image.h>
#include <pxr/imaging/hio/types.h>

#include <pxr/base/tf/type.h>

#include <algorithm>
#include <cstring>

#include <dlfcn.h>
#include <libgen.h>

namespace anari_hydra {

size_t BytesPerTexel(TexelFormat f)
{
  switch (f) {
  case TexelFormat::UNorm8:
  case TexelFormat::Srgb8:
    return 4;
  case TexelFormat::Half:
    return 8;
  case TexelFormat::Float:
    return 16;
  }
  return 16;
}

TexelFormat TexelFormatFor(ANARIDataType elementType, bool transformed)
{
  bool fixed8 = false, srgb = false, alphaInGreen = false;
  switch (elementType) {
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4:
    fixed8 = true;
    break;
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
    fixed8 = srgb = true;
    break;
  case ANARI_UFIXED8_RA_SRGB:
    fixed8 = srgb = alphaInGreen = true;
    break;
  default:
    break;
  }
  if (!fixed8 || transformed)
    return TexelFormat::Float;
  if (!srgb)
    return TexelFormat::UNorm8;
  return alphaInGreen ? TexelFormat::Half : TexelFormat::Srgb8;
}

void AnariImageData::allocate(int w, int h, TexelFormat f)
{
  width = w;
  height = h;
  format = f;
  texels.assign(size_t(w) * h * BytesPerTexel(f), 0);
}

void AnariImageData::write(size_t i, const anari::math::float4 &c)
{
  const float ch[4] = {c.x, c.y, c.z, c.w};
  uint8_t *p = &texels[i * BytesPerTexel(format)];
  switch (format) {
  case TexelFormat::UNorm8:
    for (int k = 0; k < 4; ++k) {
      const float v = ch[k] < 0.f ? 0.f : (ch[k] > 1.f ? 1.f : ch[k]);
      p[k] = uint8_t(v * 255.f + 0.5f);
    }
    break;
  case TexelFormat::Srgb8:
    for (int k = 0; k < 4; ++k) {
      float v = ch[k] < 0.f ? 0.f : (ch[k] > 1.f ? 1.f : ch[k]);
      if (k < 3)
        v = helium::toneMap<helium::ToneMapMode::TO_SRGB>(v);
      p[k] = uint8_t(v * 255.f + 0.5f);
    }
    break;
  case TexelFormat::Half: {
    constexpr float kHalfMax = 65504.f;
    pxr::GfHalf h[4];
    for (int k = 0; k < 4; ++k)
      h[k] = pxr::GfHalf(std::clamp(ch[k], -kHalfMax, kHalfMax));
    std::memcpy(p, h, sizeof(h));
    break;
  }
  case TexelFormat::Float:
    std::memcpy(p, ch, sizeof(ch));
    break;
  }
}

anari::math::float4 AnariImageData::read(size_t i) const
{
  const uint8_t *p = texel(i);
  float ch[4];
  switch (format) {
  case TexelFormat::UNorm8:
    for (int k = 0; k < 4; ++k)
      ch[k] = p[k] / 255.f;
    break;
  case TexelFormat::Srgb8:
    for (int k = 0; k < 4; ++k) {
      ch[k] = p[k] / 255.f;
      if (k < 3)
        ch[k] = helium::toneMap<helium::ToneMapMode::FROM_SRGB>(ch[k]);
    }
    break;
  case TexelFormat::Half: {
    pxr::GfHalf h[4];
    std::memcpy(h, p, sizeof(h));
    for (int k = 0; k < 4; ++k)
      ch[k] = float(h[k]);
    break;
  }
  case TexelFormat::Float:
    std::memcpy(ch, p, sizeof(ch));
    break;
  }
  return anari::math::float4(ch[0], ch[1], ch[2], ch[3]);
}

// The storage format for a decode: the source's natural precision, capped. A
// border colour is an arbitrary float value, so a padded image is stored like
// a transformed one.
static TexelFormat StorageFormatFor(ANARIDataType elementType,
    const TexelAffine *transform,
    const TexelBorder *border,
    TexelFormat cap)
{
  const bool padded = border && (border->padX || border->padY);
  return std::min(TexelFormatFor(elementType, transform || padded), cap);
}

static void StoreTexel(AnariImageData &out,
    size_t i,
    anari::math::float4 c,
    const TexelAffine *transform)
{
  if (transform)
    c = transform->apply(c);
  out.write(i, c);
}

// Allocate `out` for a w×h source framed per `border`, fill the frame with the
// (transformed) border colour, and return the interior origin.
static anari::math::int2 AllocateFramed(AnariImageData &out,
    int w,
    int h,
    TexelFormat format,
    const TexelAffine *transform,
    const TexelBorder *border)
{
  const int px = border && border->padX ? 1 : 0;
  const int py = border && border->padY ? 1 : 0;
  out.allocate(w + 2 * px, h + 2 * py, format);
  if (px || py) {
    for (int y = 0; y < out.height; ++y)
      for (int x = 0; x < out.width; ++x) {
        const bool frame = x < px || x >= px + w || y < py || y >= py + h;
        if (frame)
          StoreTexel(out, size_t(y) * out.width + x, border->color, transform);
      }
  }
  return anari::math::int2(px, py);
}

// Copy `n` 8-bit sRGB source texels of `type` (R, RGB or RGBA; see
// TexelFormatFor) into Srgb8 storage unchanged, widened to RGBA as helium's
// readAsAttributeValue widens them: missing colour channels are 0, a missing
// alpha is opaque.
static void CopySrgb8(
    const void *src, ANARIDataType type, size_t n, AnariImageData &out)
{
  const auto *s = static_cast<const uint8_t *>(src);
  uint8_t *d = out.texels.data();
  if (type == ANARI_UFIXED8_RGBA_SRGB) {
    std::memcpy(d, s, n * 4);
    return;
  }
  const bool rgb = type == ANARI_UFIXED8_RGB_SRGB;
  const size_t stride = rgb ? 3 : 1;
  for (size_t i = 0; i < n; ++i, s += stride, d += 4) {
    d[0] = s[0];
    d[1] = rgb ? s[1] : 0;
    d[2] = rgb ? s[2] : 0;
    d[3] = 255;
  }
}

bool DecodeArray2DToImage(helium::Array2D *img,
    AnariImageData &out,
    const TexelAffine *transform,
    TexelFormat maxFormat,
    const TexelBorder *border)
{
  if (!img)
    return false;
  const anari::math::uint2 sz = img->size();
  const int w = int(sz.x), h = int(sz.y);
  const TexelFormat format =
      StorageFormatFor(img->elementType(), transform, border, maxFormat);
  if (format == TexelFormat::Srgb8
      && TexelFormatFor(img->elementType(), false) == TexelFormat::Srgb8) {
    out.allocate(w, h, format); // an sRGB source, never transformed or padded
    CopySrgb8(img->data(), img->elementType(), size_t(w) * h, out);
    return true;
  }
  const anari::math::int2 o =
      AllocateFramed(out, w, h, format, transform, border);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      StoreTexel(out,
          size_t(y + o.y) * out.width + (x + o.x),
          img->readAsAttributeValue(anari::math::int2(x, y)),
          transform);
  return true;
}

bool DecodeArray1DToImage(helium::Array1D *img,
    AnariImageData &out,
    const TexelAffine *transform,
    TexelFormat maxFormat,
    const TexelBorder *border)
{
  if (!img)
    return false;
  TexelBorder xOnly;
  if (border) {
    xOnly = *border;
    xOnly.padY = false;
  }
  const int n = int(img->size());
  const TexelFormat format =
      StorageFormatFor(img->elementType(), transform, &xOnly, maxFormat);
  if (format == TexelFormat::Srgb8
      && TexelFormatFor(img->elementType(), false) == TexelFormat::Srgb8) {
    out.allocate(n, 1, format); // an sRGB source, never transformed or padded
    CopySrgb8(img->begin(), img->elementType(), size_t(n), out);
    return true;
  }
  const anari::math::int2 o =
      AllocateFramed(out, n, 1, format, transform, &xOnly);
  for (int x = 0; x < n; ++x)
    StoreTexel(out, size_t(x + o.x), img->readAsAttributeValue(x), transform);
  return true;
}

AnariImageRegistry &AnariImageRegistry::instance()
{
  static AnariImageRegistry r;
  return r;
}

void AnariImageRegistry::set(const std::string &path, AnariImageData data)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_images[path] = std::move(data);
}

void AnariImageRegistry::erase(const std::string &path)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_images.erase(path);
}

bool AnariImageRegistry::get(const std::string &path, AnariImageData &out) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_images.find(path);
  if (it == m_images.end())
    return false;
  out = it->second;
  return true;
}

void EnsureAnariHioRegistered()
{
  static bool registered = false;
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  if (registered)
    return;

  // The plugin manifest ships in an anari_hydra/ directory beside
  // libanari_library_hydra.so. Register only that directory: the library
  // directory may be shared with other packages' plugInfo.json files.
  // PlugRegistry needs an absolute path.
  Dl_info info;
  if (dladdr(reinterpret_cast<void *>(&EnsureAnariHioRegistered), &info)
      && info.dli_fname) {
    std::vector<char> buf(
        info.dli_fname, info.dli_fname + std::strlen(info.dli_fname) + 1);
    const std::string dir = dirname(buf.data());
    pxr::PlugRegistry::GetInstance().RegisterPlugins(dir + "/anari_hydra");
  }
  registered = true;
}

} // namespace anari_hydra

// --- the Hio plugin --------------------------------------------------------

PXR_NAMESPACE_USING_DIRECTIVE

// The HioImage subclass must live in the pxr namespace (not an anonymous one)
// so its registered TfType name is exactly "AnariHioImage", matching the
// plugInfo Types key — an anonymous-namespace class registers under a mangled
// name and the plugin-declared type is left factory-less (proto 013).
PXR_NAMESPACE_OPEN_SCOPE

class AnariHioImage final : public HioImage
{
 public:
  using Base = HioImage;

  AnariHioImage() = default;
  ~AnariHioImage() override = default;

  bool Read(StorageSpec const &storage) override
  {
    return ReadCropped(0, 0, 0, 0, storage);
  }

  bool ReadCropped(int const /*cropTop*/,
      int const /*cropBottom*/,
      int const /*cropLeft*/,
      int const /*cropRight*/,
      StorageSpec const &storage) override
  {
    const int dstW = storage.width;
    const int dstH = storage.height;
    auto *dst = static_cast<unsigned char *>(storage.data);
    if (!dst || _data.texels.empty())
      return false;
    // HdSt requests exactly GetFormat() (textureUtils.cpp ReadAndConvertImage
    // sets spec.format = image->GetFormat()), served as the stored bytes. An
    // unset (Invalid) format is trusted to mean GetFormat(), not checked.
    // HdEmbree's dome loader (hdEmbree/light.cpp _LoadLightTexture) asks for
    // Float32Vec3 instead, so linear float RGB/RGBA is served by converting
    // each texel through AnariImageData::read (which decodes Srgb8). Any
    // other typed request would overrun or under-fill the caller's buffer,
    // so it is refused.
    const bool asStored =
        storage.format == HioFormatInvalid || storage.format == GetFormat();
    const int floatChannels = storage.format == HioFormatFloat32Vec3 ? 3
        : storage.format == HioFormatFloat32Vec4                     ? 4
                                                                     : 0;
    if (!asStored && floatChannels == 0)
      return false;
    const size_t bpp = asStored ? anari_hydra::BytesPerTexel(_data.format)
                                : sizeof(float) * floatChannels;
    // Serve at the requested resolution (nearest-neighbour on a target-memory
    // downscale); the UV-texture path we target never crops.
    for (int y = 0; y < dstH; ++y) {
      int sy = _data.height == dstH
          ? y
          : std::min(_data.height - 1, y * _data.height / std::max(1, dstH));
      // storage.flipped means origin lower-left: flip the source row.
      if (storage.flipped)
        sy = _data.height - 1 - sy;
      for (int x = 0; x < dstW; ++x) {
        const int sx = _data.width == dstW
            ? x
            : std::min(_data.width - 1, x * _data.width / std::max(1, dstW));
        unsigned char *out = &dst[(size_t(y) * dstW + x) * bpp];
        const size_t src = size_t(sy) * _data.width + sx;
        if (asStored) {
          std::memcpy(out, _data.texel(src), bpp);
        } else {
          const anari::math::float4 c = _data.read(src);
          const float rgba[4] = {c.x, c.y, c.z, c.w};
          std::memcpy(out, rgba, bpp);
        }
      }
    }
    return true;
  }

  bool Write(StorageSpec const &, VtDictionary const &) override
  {
    return false;
  }

  std::string const &GetFilename() const override
  {
    return _filename;
  }
  int GetWidth() const override
  {
    return _data.width;
  }
  int GetHeight() const override
  {
    return _data.height;
  }
  // The stored precision is the served format (AnariImage.h TexelFormat);
  // HdSt maps each to the same-typed HgiFormat with no conversion.
  HioFormat GetFormat() const override
  {
    switch (_data.format) {
    case anari_hydra::TexelFormat::UNorm8:
      return HioFormatUNorm8Vec4;
    case anari_hydra::TexelFormat::Srgb8:
      return HioFormatUNorm8Vec4srgb;
    case anari_hydra::TexelFormat::Half:
      return HioFormatFloat16Vec4;
    case anari_hydra::TexelFormat::Float:
      return HioFormatFloat32Vec4;
    }
    return HioFormatFloat32Vec4;
  }
  int GetBytesPerPixel() const override
  {
    return int(anari_hydra::BytesPerTexel(_data.format));
  }
  int GetNumMipLevels() const override
  {
    return 1;
  }
  bool IsColorSpaceSRGB() const override
  {
    return _data.format == anari_hydra::TexelFormat::Srgb8;
  }

  bool GetMetadata(TfToken const &, VtValue *) const override
  {
    return false;
  }
  bool GetSamplerMetadata(HioAddressDimension, HioAddressMode *) const override
  {
    return false;
  }

 protected:
  bool _OpenForReading(std::string const &filename,
      int /*subimage*/,
      int mip,
      SourceColorSpace /*sourceColorSpace*/,
      bool /*suppressErrors*/) override
  {
    // Only level 0 exists; higher mips report "no image" so GetAllMipImages
    // stops and Storm generates mips from level 0 on the GPU.
    if (mip != 0)
      return false;
    if (!anari_hydra::AnariImageRegistry::instance().get(filename, _data))
      return false;
    _filename = filename;
    return true;
  }

  bool _OpenForWriting(std::string const &) override
  {
    return false;
  }

 private:
  std::string _filename;
  anari_hydra::AnariImageData _data;
};

TF_REGISTRY_FUNCTION(TfType)
{
  using Image = AnariHioImage;
  TfType t = TfType::Define<Image, TfType::Bases<Image::Base>>();
  t.SetFactory<HioImageFactory<Image>>();
}

PXR_NAMESPACE_CLOSE_SCOPE
