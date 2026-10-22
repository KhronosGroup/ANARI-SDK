// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_sampler.isph"
#include "shading/halcyon_util.isph"

// Sampler hooks (Sampler_SampleFcn): the sampling code they share with the
// pathtracer's texture system is in shading/halcyon_sampler.isph ///////////

static varying float4 Image1D_sample(const uniform Sampler *uniform self,
    const varying HitInfo &hitInfo,
    const varying RayHit &rh)
{
  if (self->inAttribute == ATTRIBUTE_NONE)
    return defaultAttributeValue();
  return Image1D_sampleInput(self, Sampler_getInput(self, hitInfo, rh));
}

static varying float4 Image2D_sample(const uniform Sampler *uniform self,
    const varying HitInfo &hitInfo,
    const varying RayHit &rh)
{
  if (self->inAttribute == ATTRIBUTE_NONE)
    return defaultAttributeValue();
  return Image2D_sampleInput(self, Sampler_getInput(self, hitInfo, rh));
}

static varying float4 Image3D_sample(const uniform Sampler *uniform self,
    const varying HitInfo &hitInfo,
    const varying RayHit &rh)
{
  if (self->inAttribute == ATTRIBUTE_NONE)
    return defaultAttributeValue();
  return Image3D_sampleInput(self, Sampler_getInput(self, hitInfo, rh));
}

static varying float4 Transform_sample(const uniform Sampler *uniform self,
    const varying HitInfo &hitInfo,
    const varying RayHit &rh)
{
  if (self->inAttribute == ATTRIBUTE_NONE)
    return defaultAttributeValue();
  return Transform_sampleInput(self, Sampler_getInput(self, hitInfo, rh));
}

static varying float4 Primitive_sample(const uniform Sampler *uniform self,
    const varying HitInfo &hitInfo,
    const varying RayHit &rh)
{
  return Primitive_sampleAt(self, rh.hit.primID);
}

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_Sampler()
{
  return sizeof(uniform Sampler);
}

// No hook (invalid), identity transforms, zero offsets
export void Sampler_setDefaults(void *uniform _self)
{
  uniform Sampler *uniform self = object_cast<Sampler>(_self);
  self->sample = NULL;
  self->kind = SAMPLER_KIND_NONE;
  self->inAttribute = ATTRIBUTE_0;
  self->inTransform = make_mat4f_identity();
  self->inOffset = make_float4(0.f);
  self->outTransform = make_mat4f_identity();
  self->outOffset = make_float4(0.f);
  self->image.texels = NULL;
  self->image.srgbToLinear = NULL;
  self->image.texelType = TEXEL_TYPE_FLOAT32;
  self->image.components = 4;
  self->primitive.values = NULL;
  self->primitive.size = 0;
  self->primitive.offset = 0;
}

// Parameters shared by the subtypes; the matrices are packed column-major 4x4,
// the offsets packed float4
export void Sampler_setParameters(void *uniform _self,
    uniform Attribute inAttribute,
    const uniform float *uniform inTransform,
    const uniform float *uniform inOffset,
    const uniform float *uniform outTransform,
    const uniform float *uniform outOffset)
{
  uniform Sampler *uniform self = object_cast<Sampler>(_self);
  self->inAttribute = inAttribute;
  self->inTransform = load_mat4f(inTransform);
  self->inOffset = load_float4(inOffset);
  self->outTransform = load_mat4f(outTransform);
  self->outOffset = load_float4(outOffset);
}

// Image data shared by image1D/2D/3D; 'texels' are 'components' (1-4) of
// 'texelType' each, packed, x fastest, and 'size' has 1 in unused dimensions;
// 'srgbToLinear' (256 floats) is needed for TEXEL_TYPE_UFIXED8_SRGB. Installs
// 'hook', or no hook (an invalid sampler) if 'texels' is NULL.
static void setImage(void *uniform _self,
    const void *uniform texels,
    uniform TexelType texelType,
    uniform int components,
    const uniform float *uniform srgbToLinear,
    const uniform int *uniform size,
    const uniform WrapMode *uniform wrapMode,
    uniform bool linearFilter,
    uniform Sampler_SampleFcn hook,
    uniform SamplerKind kind)
{
  uniform Sampler *uniform self = object_cast<Sampler>(_self);
  uniform ImageSampler &img = self->image;
  img.texels = (const uniform uint8 *uniform)texels;
  img.srgbToLinear = srgbToLinear;
  img.texelType = texelType;
  img.components = components;
  for (uniform int d = 0; d < 3; d++) {
    img.size[d] = size[d];
    img.wrapMode[d] = wrapMode[d];
  }
  img.linearFilter = linearFilter;
  self->sample = texels != NULL ? hook : NULL;
  self->kind = texels != NULL ? kind : SAMPLER_KIND_NONE;
}

// Installs the image1D hook (see setImage())
export void Sampler_setImage1D(void *uniform _self,
    const void *uniform texels,
    uniform TexelType texelType,
    uniform int components,
    const uniform float *uniform srgbToLinear,
    const uniform int *uniform size,
    const uniform WrapMode *uniform wrapMode,
    uniform bool linearFilter)
{
  setImage(_self,
      texels,
      texelType,
      components,
      srgbToLinear,
      size,
      wrapMode,
      linearFilter,
      Image1D_sample,
      SAMPLER_KIND_IMAGE1D);
}

// Installs the image2D hook (see setImage())
export void Sampler_setImage2D(void *uniform _self,
    const void *uniform texels,
    uniform TexelType texelType,
    uniform int components,
    const uniform float *uniform srgbToLinear,
    const uniform int *uniform size,
    const uniform WrapMode *uniform wrapMode,
    uniform bool linearFilter)
{
  setImage(_self,
      texels,
      texelType,
      components,
      srgbToLinear,
      size,
      wrapMode,
      linearFilter,
      Image2D_sample,
      SAMPLER_KIND_IMAGE2D);
}

// Installs the image3D hook (see setImage())
export void Sampler_setImage3D(void *uniform _self,
    const void *uniform texels,
    uniform TexelType texelType,
    uniform int components,
    const uniform float *uniform srgbToLinear,
    const uniform int *uniform size,
    const uniform WrapMode *uniform wrapMode,
    uniform bool linearFilter)
{
  setImage(_self,
      texels,
      texelType,
      components,
      srgbToLinear,
      size,
      wrapMode,
      linearFilter,
      Image3D_sample,
      SAMPLER_KIND_IMAGE3D);
}

// Installs the transform hook; its matrix and offset are the ones set by
// Sampler_setParameters() as the out-transform and out-offset
export void Sampler_setTransform(void *uniform _self)
{
  uniform Sampler *uniform self = object_cast<Sampler>(_self);
  self->sample = Transform_sample;
  self->kind = SAMPLER_KIND_TRANSFORM;
}

// Installs the primitive hook for the 'size' packed float4 'values' (no hook,
// an invalid sampler, if 'values' is NULL or 'size' is 0)
export void Sampler_setPrimitive(void *uniform _self,
    const uniform float *uniform values,
    uniform uint64 size,
    uniform uint64 offset)
{
  uniform Sampler *uniform self = object_cast<Sampler>(_self);
  uniform PrimitiveSampler &p = self->primitive;
  p.values = values;
  p.size = size;
  // clamping the offset first keeps primID + offset from overflowing
  p.offset = size > 0 ? min(offset, size - 1) : 0;
  const uniform bool valid = values != NULL && size > 0;
  self->sample = valid ? Primitive_sample : NULL;
  self->kind = valid ? SAMPLER_KIND_PRIMITIVE : SAMPLER_KIND_NONE;
}
