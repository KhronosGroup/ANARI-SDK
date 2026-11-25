// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0
//
// Ported from Typhoon (NVIDIA-Omniverse/OpenUSD, branch typhoon/main, commit
// 70c45e83b08a7b45588cd6cdbd2ae67f5147befb),
// pxr/imaging/plugin/hdEmbree/delegate/light.cpp
// ty::BuildDomeLightSamplingDistribution() and _GetLuminance(). Copyright
// 2025 Pixar, licensed under the Tomorrow Open Source Technology License 1.0
// (see THIRD-PARTY.md). Keep in step with it when the pin in
// external/typhoon/PROVENANCE.md moves.

#include "HdriDistribution.h"
// std
#include <algorithm>
#include <cmath>

namespace halcyon {

namespace {

// Typhoon's _SanitizeWeight()
float sanitizeWeight(float value)
{
  if (!std::isfinite(value) || value < 0.0f)
    return 0.0f;
  return value;
}

// Typhoon's _GetLuminance() for its default (linear Rec. 709) colour space
float luminance(const float *rgb)
{
  return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

} // namespace

void buildHdriDistribution(const std::vector<float> &texels,
    int width,
    int height,
    std::vector<float> &texelWeights,
    std::vector<float> &conditionalCdf,
    std::vector<float> &marginalCdf,
    float &weightSum)
{
  constexpr float pi = static_cast<float>(M_PI);
  const size_t texelCount = size_t(width) * size_t(height);
  texelWeights.assign(texelCount, 0.0f);
  conditionalCdf.assign(size_t(height) * size_t(width + 1), 0.0f);
  marginalCdf.assign(size_t(height + 1), 0.0f);
  weightSum = 0.0f;

  float totalWeight = 0.0f;
  marginalCdf[0] = 0.0f;
  for (int y = 0; y < height; ++y) {
    const float theta0 = pi * (static_cast<float>(y) / float(height));
    const float theta1 = pi * ((static_cast<float>(y) + 1.0f) / float(height));
    const float rowMeasure =
        std::max(0.0f, std::cos(theta0) - std::cos(theta1));

    float rowWeight = 0.0f;
    float *const rowCdf = conditionalCdf.data() + size_t(y) * size_t(width + 1);
    rowCdf[0] = 0.0f;
    for (int x = 0; x < width; ++x) {
      const size_t idx = size_t(y) * size_t(width) + x;
      const float weight =
          sanitizeWeight(luminance(texels.data() + 3 * idx)) * rowMeasure;
      texelWeights[idx] = weight;
      rowWeight += weight;
      rowCdf[x + 1] = rowWeight;
    }

    if (rowWeight <= 0.0f) {
      rowWeight = 0.0f;
      for (int x = 0; x < width; ++x) {
        const size_t idx = size_t(y) * size_t(width) + x;
        texelWeights[idx] = rowMeasure;
        rowWeight += rowMeasure;
        rowCdf[x + 1] = rowWeight;
      }
    }

    if (rowWeight > 0.0f) {
      const float invRowWeight = 1.0f / rowWeight;
      for (int x = 1; x <= width; ++x)
        rowCdf[x] *= invRowWeight;
      rowCdf[width] = 1.0f;
    }

    totalWeight += rowWeight;
    marginalCdf[y + 1] = totalWeight;
  }

  if (totalWeight <= 0.0f) {
    texelWeights.clear();
    conditionalCdf.clear();
    marginalCdf.clear();
    return;
  }

  const float invTotalWeight = 1.0f / totalWeight;
  for (size_t i = 1; i < marginalCdf.size(); ++i)
    marginalCdf[i] *= invTotalWeight;
  marginalCdf.back() = 1.0f;
  weightSum = totalWeight;
}


} // namespace halcyon
