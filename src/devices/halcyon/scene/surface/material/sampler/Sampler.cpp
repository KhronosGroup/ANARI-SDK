// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"
#include "Sampler_ispc.h"
#include "array/Array1D.h"
#include "array/Array2D.h"
#include "array/Array3D.h"
// std
#include <vector>

namespace halcyon {

// Sampler subtypes ///////////////////////////////////////////////////////////

// The input attribute and in/out transforms a subtype's sample is mapped with:
// outTransform * f(inTransform * inAttribute + inOffset) + outOffset
struct SamplerMapping
{
  Attribute inAttribute{Attribute::ATTRIBUTE_0};
  mat4 inTransform{linalg::identity};
  float4 inOffset{0.f};
  mat4 outTransform{linalg::identity};
  float4 outOffset{0.f};

  // Passes the mapping to sampler ISPC object 'ie'
  void setISPC(void *ie) const
  {
    // ISPC's Attribute enum follows helium's order
    ispc::Sampler_setParameters(ie,
        ispc::Attribute(inAttribute),
        &inTransform[0].x,
        &inOffset.x,
        &outTransform[0].x,
        &outOffset.x);
  }
};

// 'inAttribute' of sampler 's'
static Attribute inAttributeParam(const Sampler &s)
{
  return attributeFromString(s.getParamString("inAttribute", "attribute0"));
}

// image1D/2D/3D: the image is copied to float4 texels at finalize; subtypes
// read their 'image' and wrap modes and report the image's size. A missing or
// empty image (any dimension 0) makes the sampler invalid.
struct ImageSampler : public Sampler
{
  ImageSampler(HalcyonDeviceGlobalState *s) : Sampler(s) {}

  bool isValid() const override
  {
    return Sampler::isValid() && m_image && !m_texels.empty();
  }

  void commitParameters() override
  {
    Sampler::commitParameters();
    m_linearFilter = getParamString("filter", "linear") != "nearest";
    m_mapping.inAttribute = inAttributeParam(*this);
    m_mapping.inTransform =
        getParam<mat4>("inTransform", mat4(linalg::identity));
    m_mapping.inOffset = getParam<float4>("inOffset", float4(0.f));
    m_mapping.outTransform =
        getParam<mat4>("outTransform", mat4(linalg::identity));
    m_mapping.outOffset = getParam<float4>("outOffset", float4(0.f));
  }

  void finalize() override
  {
    m_texels = toAttributeValues(m_image.get());
    if (m_image && m_texels.empty()) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "image sampler's 'image' is empty; the sampler is invalid");
    }

    m_mapping.setISPC(getIE());

    const uint3 size = m_texels.empty() ? uint3(1u) : imageSize();
    const int32_t isize[3] = {
        int32_t(size.x), int32_t(size.y), int32_t(size.z)};
    // ISPC's WrapMode enum follows helium's order
    const ispc::WrapMode wrap[3] = {ispc::WrapMode(m_wrapMode[0]),
        ispc::WrapMode(m_wrapMode[1]),
        ispc::WrapMode(m_wrapMode[2])};
    // no image (including an empty one) leaves the sampler without a hook
    setISPCImage(m_texels.empty() ? nullptr : (const float *)m_texels.data(),
        isize,
        wrap,
        m_linearFilter);
  }

 protected:
  // 'image' dimensions, 1 in unused ones; only called with an image set
  virtual uint3 imageSize() const = 0;
  // Passes the image to ISPC with the subtype's Sampler_setImage<N>D()
  virtual void setISPCImage(const float *texels,
      const int32_t *size,
      const ispc::WrapMode *wrapMode,
      bool linearFilter) = 0;

  WrapMode wrapModeParam(const char *name) const
  {
    return wrapModeFromString(getParamString(name, "clampToEdge"));
  }

  helium::ChangeObserverPtr<helium::Array> m_image{this};
  WrapMode m_wrapMode[3]{
      WrapMode::DEFAULT, WrapMode::DEFAULT, WrapMode::DEFAULT};

 private:
  SamplerMapping m_mapping;
  bool m_linearFilter{true};
  std::vector<float4> m_texels;
};

struct Image1D : public ImageSampler
{
  Image1D(HalcyonDeviceGlobalState *s) : ImageSampler(s) {}

  void commitParameters() override
  {
    ImageSampler::commitParameters();
    m_image = getParamObject<Array1D>("image");
    m_wrapMode[0] = wrapModeParam("wrapMode");
  }

 private:
  uint3 imageSize() const override
  {
    return uint3(uint32_t(((const Array1D *)m_image.get())->size()), 1u, 1u);
  }

  void setISPCImage(const float *texels,
      const int32_t *size,
      const ispc::WrapMode *wrapMode,
      bool linearFilter) override
  {
    ispc::Sampler_setImage1D(getIE(), texels, size, wrapMode, linearFilter);
  }
};

struct Image2D : public ImageSampler
{
  Image2D(HalcyonDeviceGlobalState *s) : ImageSampler(s) {}

  void commitParameters() override
  {
    ImageSampler::commitParameters();
    m_image = getParamObject<Array2D>("image");
    m_wrapMode[0] = wrapModeParam("wrapMode1");
    m_wrapMode[1] = wrapModeParam("wrapMode2");
  }

 private:
  uint3 imageSize() const override
  {
    return uint3(((const Array2D *)m_image.get())->size(), 1u);
  }

  void setISPCImage(const float *texels,
      const int32_t *size,
      const ispc::WrapMode *wrapMode,
      bool linearFilter) override
  {
    ispc::Sampler_setImage2D(getIE(), texels, size, wrapMode, linearFilter);
  }
};

// Reads 'inOffset' and 'outOffset' like the others (helide's ignores them)
struct Image3D : public ImageSampler
{
  Image3D(HalcyonDeviceGlobalState *s) : ImageSampler(s) {}

  void commitParameters() override
  {
    ImageSampler::commitParameters();
    m_image = getParamObject<Array3D>("image");
    m_wrapMode[0] = wrapModeParam("wrapMode1");
    m_wrapMode[1] = wrapModeParam("wrapMode2");
    m_wrapMode[2] = wrapModeParam("wrapMode3");
  }

 private:
  uint3 imageSize() const override
  {
    return ((const Array3D *)m_image.get())->size();
  }

  void setISPCImage(const float *texels,
      const int32_t *size,
      const ispc::WrapMode *wrapMode,
      bool linearFilter) override
  {
    ispc::Sampler_setImage3D(getIE(), texels, size, wrapMode, linearFilter);
  }
};

// transform: 'outTransform' * inAttribute + 'outOffset' (the spec's names;
// helide's 'transform' is not read).
struct TransformSampler : public Sampler
{
  TransformSampler(HalcyonDeviceGlobalState *s) : Sampler(s) {}

  void commitParameters() override
  {
    Sampler::commitParameters();
    // the in-transform stays the identity
    m_mapping.inAttribute = inAttributeParam(*this);
    m_mapping.outTransform =
        getParam<mat4>("outTransform", mat4(linalg::identity));
    m_mapping.outOffset = getParam<float4>("outOffset", float4(0.f));
  }

  void finalize() override
  {
    m_mapping.setISPC(getIE());
    ispc::Sampler_setTransform(getIE());
  }

 private:
  SamplerMapping m_mapping;
};

// primitive: 'array'[primitive index + 'inOffset'], the index clamped to the
// array. The array is copied to float4 values at finalize. helide's 'offset'
// name is not read.
struct PrimitiveSampler : public Sampler
{
  PrimitiveSampler(HalcyonDeviceGlobalState *s) : Sampler(s) {}

  bool isValid() const override
  {
    return Sampler::isValid() && m_array;
  }

  void commitParameters() override
  {
    Sampler::commitParameters();
    m_array = getParamObject<Array1D>("array");
    m_offset = inOffsetParam();
  }

  void finalize() override
  {
    m_values = toAttributeValues(m_array.get());
    // no array (including an empty one) leaves the sampler without a hook
    ispc::Sampler_setPrimitive(getIE(),
        m_values.empty() ? nullptr : (const float *)m_values.data(),
        m_values.size(),
        m_offset);
  }

 private:
  // 'inOffset' as uint64 (the spec's type) or uint32, 0 if it has neither
  uint64_t inOffsetParam() const
  {
    if (hasParam("inOffset", ANARI_UINT32))
      return getParam<uint32_t>("inOffset", 0);
    return getParam<uint64_t>("inOffset", 0);
  }

  helium::ChangeObserverPtr<Array1D> m_array{this};
  uint64_t m_offset{0};
  std::vector<float4> m_values;
};

// Base Sampler definitions ///////////////////////////////////////////////////

Sampler::Sampler(HalcyonDeviceGlobalState *s)
    : Object(ANARI_SAMPLER, s, ispc::sizeof_Sampler())
{
  // helium only finalizes objects with committed parameters: until then the
  // sampler has no hook
  ispc::Sampler_setDefaults(getIE());
}

Sampler *Sampler::createInstance(
    std::string_view subtype, HalcyonDeviceGlobalState *s)
{
  if (subtype == "image1D")
    return new Image1D(s);
  else if (subtype == "image2D")
    return new Image2D(s);
  else if (subtype == "image3D")
    return new Image3D(s);
  else if (subtype == "transform")
    return new TransformSampler(s);
  else if (subtype == "primitive")
    return new PrimitiveSampler(s);
  else {
    // materials may reference unknown samplers too, which never sample
    auto *unknown = new UnknownObject(ANARI_SAMPLER, s, ispc::sizeof_Sampler());
    ispc::Sampler_setDefaults(unknown->getIE());
    return (Sampler *)unknown;
  }
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Sampler *);
