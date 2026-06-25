// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Metrics.h"
// std
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace anari {
namespace cts {

namespace {

constexpr double kDataRange = 255.0;

bool comparable(const Image &a, const Image &b)
{
  return a.valid() && b.valid() && a.width == b.width && a.height == b.height;
}

// One channel of an image, with alpha composited over a white background and
// rounded to 8 bits (matching skimage rgba2rgb + img_as_ubyte). Values 0..255.
std::vector<double> compositedChannel(const Image &img, int c)
{
  std::vector<double> out(img.pixelCount());
  for (size_t i = 0; i < out.size(); ++i) {
    const double v = img.rgba[i * 4 + c];
    const double a = img.rgba[i * 4 + 3];
    double composited = v * a / kDataRange + (kDataRange - a);
    composited = std::round(composited);
    out[i] = composited < 0.0 ? 0.0 : (composited > 255.0 ? 255.0 : composited);
  }
  return out;
}

// scipy 'reflect' boundary: edge sample duplicated (-1 -> 0, N -> N-1).
int reflectIndex(int i, int n)
{
  if (n == 1)
    return 0;
  while (i < 0 || i >= n) {
    if (i < 0)
      i = -i - 1;
    if (i >= n)
      i = 2 * n - i - 1;
  }
  return i;
}

// Separable 7x7 box mean with reflect boundary == scipy uniform_filter(size=7).
std::vector<double> boxMean(
    const std::vector<double> &in, int w, int h, int win)
{
  const int r = win / 2;
  const double inv = 1.0 / win;

  std::vector<double> tmp(in.size());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      double sum = 0.0;
      for (int k = -r; k <= r; ++k)
        sum += in[static_cast<size_t>(y) * w + reflectIndex(x + k, w)];
      tmp[static_cast<size_t>(y) * w + x] = sum * inv;
    }
  }

  std::vector<double> out(in.size());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      double sum = 0.0;
      for (int k = -r; k <= r; ++k)
        sum += tmp[static_cast<size_t>(reflectIndex(y + k, h)) * w + x];
      out[static_cast<size_t>(y) * w + x] = sum * inv;
    }
  }
  return out;
}

double ssimChannel(
    const std::vector<double> &x, const std::vector<double> &y, int w, int h)
{
  constexpr int win = 7;
  const double c1 = (0.01 * kDataRange) * (0.01 * kDataRange);
  const double c2 = (0.03 * kDataRange) * (0.03 * kDataRange);
  const double np = static_cast<double>(win) * win;
  const double covNorm = np / (np - 1.0); // sample covariance

  const size_t n = x.size();
  std::vector<double> xx(n), yy(n), xy(n);
  for (size_t i = 0; i < n; ++i) {
    xx[i] = x[i] * x[i];
    yy[i] = y[i] * y[i];
    xy[i] = x[i] * y[i];
  }

  const auto ux = boxMean(x, w, h, win);
  const auto uy = boxMean(y, w, h, win);
  const auto uxx = boxMean(xx, w, h, win);
  const auto uyy = boxMean(yy, w, h, win);
  const auto uxy = boxMean(xy, w, h, win);

  // Average the SSIM map over the valid interior (crop the window border).
  const int pad = (win - 1) / 2;
  double sum = 0.0;
  size_t count = 0;
  for (int yy2 = pad; yy2 < h - pad; ++yy2) {
    for (int xx2 = pad; xx2 < w - pad; ++xx2) {
      const size_t i = static_cast<size_t>(yy2) * w + xx2;
      const double vx = covNorm * (uxx[i] - ux[i] * ux[i]);
      const double vy = covNorm * (uyy[i] - uy[i] * uy[i]);
      const double vxy = covNorm * (uxy[i] - ux[i] * uy[i]);
      const double a1 = 2.0 * ux[i] * uy[i] + c1;
      const double a2 = 2.0 * vxy + c2;
      const double b1 = ux[i] * ux[i] + uy[i] * uy[i] + c1;
      const double b2 = vx + vy + c2;
      sum += (a1 * a2) / (b1 * b2);
      ++count;
    }
  }
  return count ? sum / static_cast<double>(count)
               : std::numeric_limits<double>::quiet_NaN();
}

} // namespace

double psnr(const Image &reference, const Image &candidate)
{
  if (!comparable(reference, candidate))
    return std::numeric_limits<double>::quiet_NaN();

  double squaredError = 0.0;
  size_t count = 0;
  for (int c = 0; c < 3; ++c) {
    const auto r = compositedChannel(reference, c);
    const auto k = compositedChannel(candidate, c);
    for (size_t i = 0; i < r.size(); ++i) {
      const double d = r[i] - k[i];
      squaredError += d * d;
      ++count;
    }
  }

  if (count == 0)
    return std::numeric_limits<double>::quiet_NaN();

  const double mse = squaredError / static_cast<double>(count);
  if (mse == 0.0)
    return std::numeric_limits<double>::infinity();

  return 10.0 * std::log10(kDataRange * kDataRange / mse);
}

double ssim(const Image &reference, const Image &candidate)
{
  if (!comparable(reference, candidate))
    return std::numeric_limits<double>::quiet_NaN();

  constexpr int win = 7;
  const int w = static_cast<int>(reference.width);
  const int h = static_cast<int>(reference.height);
  if (w < win || h < win)
    return std::numeric_limits<double>::quiet_NaN();

  double sum = 0.0;
  for (int c = 0; c < 3; ++c) {
    const auto x = compositedChannel(reference, c);
    const auto y = compositedChannel(candidate, c);
    sum += ssimChannel(x, y, w, h);
  }
  return sum / 3.0;
}

} // namespace cts
} // namespace anari
