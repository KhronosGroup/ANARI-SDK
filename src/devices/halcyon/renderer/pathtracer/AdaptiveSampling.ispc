// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0
//
// AdaptiveSampling.isph's functions, in their own file compiled without fused
// multiply-adds (--opt=disable-fma, CMakeLists.txt) so that they round as the
// C++ they port does (x86-64 builds have no FMA): a pixel converges after the
// same sample as in Typhoon.
//
// Ported from Typhoon (NVIDIA-Omniverse/OpenUSD typhoon/main at 70c45e83b,
// pxr/imaging/plugin/hdEmbree/renderer/aov/aovOutput.cpp:
// ty::Renderer::_UpdateVariance() and _IsPerChannelVarianceConverged()),
// Copyright 2018 Pixar, Tomorrow Open Source Technology License 1.0; see
// external/typhoon/LICENSE.txt. Keep in step with those when the pin in
// external/typhoon/PROVENANCE.md moves.

#include "AdaptiveSampling.isph"

// _kAdaptiveAbsoluteStdError: the standard error every channel may keep
#define ADAPTIVE_ABSOLUTE_STD_ERROR 0.001f

// 'v' times 1 / 's', as GfVec3f's operator/(double) computes it: in double,
// rounded to float
static inline float divideAsGf(float v, float s)
{
  return (float)((double)v * (1.d / (double)s));
}

void AdaptiveSampling_addSample(
    float4 &mean, float3 &m2, float4 sample, int count)
{
  const float n = (float)count;
  // GfVec3f delta = rgb - mean; mean += delta / count
  const float3 delta = make_float3(sample) - make_float3(mean);
  mean.x = mean.x + divideAsGf(delta.x, n);
  mean.y = mean.y + divideAsGf(delta.y, n);
  mean.z = mean.z + divideAsGf(delta.z, n);
  mean.w = mean.w + divideAsGf(sample.w - mean.w, n);
  // GfVec3f delta2 = rgb - mean; M2 += GfCompMult(delta, delta2)
  const float3 delta2 = make_float3(sample) - make_float3(mean);
  const float3 product = delta * delta2;
  m2 = m2 + product;
}

bool AdaptiveSampling_converged(float3 mean,
    float3 m2,
    int count,
    uniform float threshold,
    uniform int minSamples)
{
  // compared as unsigned, as in Typhoon: a negative 'minSamples' never
  // converges
  if ((unsigned int32)count < (unsigned int32)minSamples)
    return false;
  const float n = (float)count;
  const float nSquared = n * n;
  const float3 varOfMean = make_float3(divideAsGf(m2.x, nSquared),
      divideAsGf(m2.y, nSquared),
      divideAsGf(m2.z, nSquared));
  const uniform float t = max(0.f, threshold);
  const uniform float absoluteVariance =
      ADAPTIVE_ABSOLUTE_STD_ERROR * ADAPTIVE_ABSOLUTE_STD_ERROR;
  const float3 m = abs(mean);
  // threshold * |mean| * |mean|, left to right as in Typhoon
  const float3 relative = (t * m) * m;
  const float3 limit = absoluteVariance + relative;
  // not '<=': a NaN variance counts as converged, as in Typhoon
  return !(varOfMean.x > limit.x) && !(varOfMean.y > limit.y)
      && !(varOfMean.z > limit.z);
}
