// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Single-lane entry points into halcyon's sampling code
// (shading/halcyon_sampler.isph, which the preview renderer's sampler hooks
// run too) for the pathtracer's texture system, HalcyonTextureSystem.h. They
// read a sampler's ISPC object, which must not change while they run: its
// owner rewrites it only in finalize(), never during a render.

#include "shading/halcyon_sampler.isph"

// The kind of sampler 'sampler' (an ISPC Sampler), SAMPLER_KIND_NONE if it is
// NULL or invalid (has no hook)
export uniform SamplerKind HalcyonTexture_kind(const void *uniform sampler)
{
  const uniform Sampler *uniform s = (const uniform Sampler *uniform)sampler;
  return Sampler_isValid(s) ? s->kind : SAMPLER_KIND_NONE;
}

// The attribute the sampler's input comes from: its inAttribute, or
// ATTRIBUTE_NONE if it reads none (invalid and primitive samplers, and
// inAttribute "none")
export uniform Attribute HalcyonTexture_inAttribute(
    const void *uniform sampler)
{
  const uniform Sampler *uniform s = (const uniform Sampler *uniform)sampler;
  const uniform SamplerKind kind = HalcyonTexture_kind(sampler);
  return kind == SAMPLER_KIND_NONE || kind == SAMPLER_KIND_PRIMITIVE
      ? ATTRIBUTE_NONE
      : s->inAttribute;
}

// Writes to 'result' (float4) the value of sampler 'sampler' for its
// inAttribute's value 'attribute' (float4) at Embree primitive 'primID', what
// the sampler's hook returns at such a hit; false (and 'result' untouched) if
// the sampler is NULL or invalid
export uniform bool HalcyonTexture_sample(const void *uniform sampler,
    const uniform float *uniform attribute,
    uniform uint32 primID,
    uniform float *uniform result)
{
  const uniform Sampler *uniform s = (const uniform Sampler *uniform)sampler;
  if (!Sampler_isValid(s))
    return false;
  const float4 a =
      make_float4(attribute[0], attribute[1], attribute[2], attribute[3]);
  const float4 v = Sampler_sampleAttribute(s, a, primID);
  result[0] = extract(v.x, 0);
  result[1] = extract(v.y, 0);
  result[2] = extract(v.z, 0);
  result[3] = extract(v.w, 0);
  return true;
}

// Writes to 'result' (float4) image 'texels' ('width' x 'height' packed
// float4 texels, x fastest) at texture coordinates (s, t), wrapped by
// 'wrapS' and 'wrapT', filtered linearly or to the nearest texel, with the
// image2D sampler's code: the pathtracer's image files
// (HalcyonTextureSystem::Sample2D(), ImageFile.h)
export void HalcyonTexture_sampleImage(const uniform float *uniform texels,
    uniform int width,
    uniform int height,
    uniform WrapMode wrapS,
    uniform WrapMode wrapT,
    uniform bool linear,
    uniform float s,
    uniform float t,
    uniform float *uniform result)
{
  uniform ImageSampler img;
  img.texels = texels;
  img.size[0] = width;
  img.size[1] = height;
  img.size[2] = 1;
  img.wrapMode[0] = wrapS;
  img.wrapMode[1] = wrapT;
  img.wrapMode[2] = WRAP_MODE_CLAMP_TO_EDGE;
  img.linearFilter = linear;
  const float4 v = Image2D_filterAt(img, make_float4(s, t, 0.f, 1.f));
  result[0] = extract(v.x, 0);
  result[1] = extract(v.y, 0);
  result[2] = extract(v.z, 0);
  result[3] = extract(v.w, 0);
}
