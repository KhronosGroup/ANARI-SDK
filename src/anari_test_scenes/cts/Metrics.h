// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Image.h"
#include "Export.h"

namespace anari {
namespace cts {

// The C++ runner is the single source of image-comparison metrics (ADR-0004).
// Both metrics compare the RGB channels with the alpha composited over a white
// background and a data range of 255, matching the old scikit-image pipeline so
// the historical thresholds (ssim 0.70, psnr 20 dB) keep their meaning.

// Peak signal-to-noise ratio in decibels. +infinity for identical images, NaN
// if the two images differ in size or are invalid. Higher is better.
// Mirrors skimage.metrics.peak_signal_noise_ratio.
ANARI_CTS_CORE_INTERFACE double psnr(
    const Image &reference, const Image &candidate);

// Mean structural similarity in [-1, 1], averaged over the RGB channels with a
// 7x7 uniform window and sample covariance. 1.0 means identical. NaN if the
// images differ in size, are invalid, or are smaller than the window.
// Mirrors skimage.metrics.structural_similarity(..., channel_axis=2).
ANARI_CTS_CORE_INTERFACE double ssim(
    const Image &reference, const Image &candidate);

// Whether a score clears its threshold. Both metrics are higher-is-better, so
// this is score > threshold; a NaN score (e.g. mismatched images) never passes.
inline bool metricPassed(double score, double threshold)
{
  return score > threshold;
}

} // namespace cts
} // namespace anari
