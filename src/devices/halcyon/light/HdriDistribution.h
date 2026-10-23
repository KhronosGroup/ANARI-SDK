// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <vector>

namespace halcyon {

// Typhoon's BuildDomeLightSamplingDistribution() (delegate/light.cpp) over
// 'texels' (width x height packed float3s, row 0 at the top, Typhoon's
// order), for the hdri light (Hdri.cpp): each texel weighs its Rec. 709
// luminance times its row's solid-angle measure, a black row weighs its
// texels by the measure alone, and a black map clears the distribution
// (empty arrays, weightSum 0: uniform sampling). texelWeights gets
// width * height entries, conditionalCdf height * (width + 1), marginalCdf
// height + 1.
void buildHdriDistribution(const std::vector<float> &texels,
    int width,
    int height,
    std::vector<float> &texelWeights,
    std::vector<float> &conditionalCdf,
    std::vector<float> &marginalCdf,
    float &weightSum);

} // namespace halcyon
