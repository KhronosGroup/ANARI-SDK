// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts
#include "cts/Image.h"
#include "cts/Metrics.h"
// std
#include <cmath>
#include <filesystem>

using namespace anari::cts;

namespace {

Image solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
  Image im;
  im.width = w;
  im.height = h;
  im.rgba.resize(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < im.pixelCount(); ++i) {
    im.rgba[i * 4 + 0] = r;
    im.rgba[i * 4 + 1] = g;
    im.rgba[i * 4 + 2] = b;
    im.rgba[i * 4 + 3] = a;
  }
  return im;
}

// A deterministic opaque gradient, optionally offset to perturb every pixel.
Image gradient(uint32_t w, uint32_t h, int offset = 0)
{
  Image im;
  im.width = w;
  im.height = h;
  im.rgba.resize(static_cast<size_t>(w) * h * 4);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const size_t i = (static_cast<size_t>(y) * w + x) * 4;
      int v = static_cast<int>((x * 7 + y * 3)) + offset;
      v = v < 0 ? 0 : (v > 255 ? 255 : v);
      im.rgba[i + 0] = static_cast<uint8_t>(v);
      im.rgba[i + 1] = static_cast<uint8_t>((v * 2) % 256);
      im.rgba[i + 2] = static_cast<uint8_t>((v + 64) % 256);
      im.rgba[i + 3] = 255;
    }
  }
  return im;
}

} // namespace

// PSNR ///////////////////////////////////////////////////////////////////////

TEST_CASE("psnr is infinite for identical images", "[cts][metrics]")
{
  auto a = gradient(32, 32);
  CHECK(std::isinf(psnr(a, a)));
  CHECK(psnr(a, a) > 0.0);
}

TEST_CASE("psnr matches the closed form for a constant offset", "[cts][metrics]")
{
  // Every channel differs by 10 -> MSE = 100 -> 10*log10(255^2/100).
  auto a = solid(8, 8, 100, 100, 100, 255);
  auto b = solid(8, 8, 110, 110, 110, 255);
  const double expected = 10.0 * std::log10(255.0 * 255.0 / 100.0);
  CHECK(psnr(a, b) == Approx(expected).epsilon(0.001));
}

TEST_CASE("psnr decreases as images diverge", "[cts][metrics]")
{
  auto ref = gradient(32, 32);
  const double near = psnr(ref, gradient(32, 32, 5));
  const double far = psnr(ref, gradient(32, 32, 40));
  CHECK(near > far);
}

TEST_CASE("psnr is NaN for mismatched sizes", "[cts][metrics]")
{
  CHECK(std::isnan(psnr(solid(8, 8, 0, 0, 0, 255), solid(16, 16, 0, 0, 0, 255))));
}

// SSIM ///////////////////////////////////////////////////////////////////////

TEST_CASE("ssim is 1.0 for identical images", "[cts][metrics]")
{
  auto a = gradient(32, 32);
  CHECK(ssim(a, a) == Approx(1.0));
}

TEST_CASE("ssim decreases as images diverge", "[cts][metrics]")
{
  auto ref = gradient(64, 64);
  const double same = ssim(ref, ref);
  const double near = ssim(ref, gradient(64, 64, 8));
  const double far = ssim(ref, gradient(64, 64, 80));
  CHECK(same == Approx(1.0));
  CHECK(same > near);
  CHECK(near > far);
  CHECK(far < 1.0);
}

TEST_CASE("ssim stays within [-1, 1]", "[cts][metrics]")
{
  auto black = solid(32, 32, 0, 0, 0, 255);
  auto white = solid(32, 32, 255, 255, 255, 255);
  const double s = ssim(black, white);
  CHECK(s <= 1.0);
  CHECK(s >= -1.0);
}

TEST_CASE("ssim is NaN when smaller than the window or mismatched",
    "[cts][metrics]")
{
  CHECK(std::isnan(ssim(solid(4, 4, 0, 0, 0, 255), solid(4, 4, 0, 0, 0, 255))));
  CHECK(std::isnan(
      ssim(solid(32, 32, 0, 0, 0, 255), solid(16, 16, 0, 0, 0, 255))));
}

// Alpha compositing //////////////////////////////////////////////////////////

TEST_CASE("metrics composite alpha over white before comparing", "[cts][metrics]")
{
  // Fully transparent pixels composite to white regardless of their RGB, so a
  // transparent-black image and an opaque-white image are identical.
  auto transparentBlack = solid(16, 16, 0, 0, 0, 0);
  auto opaqueWhite = solid(16, 16, 255, 255, 255, 255);
  CHECK(std::isinf(psnr(transparentBlack, opaqueWhite)));
  CHECK(ssim(transparentBlack, opaqueWhite) == Approx(1.0));
}

// PNG round-trip /////////////////////////////////////////////////////////////

TEST_CASE("savePNG/loadPNG round-trips RGBA8 pixels", "[cts][metrics][image]")
{
  auto image = gradient(24, 16);

  const auto dir = std::filesystem::temp_directory_path()
      / "cts_image_test" / "nested";
  const auto path = (dir / "roundtrip.png").string();
  std::error_code ec;
  std::filesystem::remove_all(dir.parent_path(), ec);

  REQUIRE(savePNG(path, image)); // also creates the nested directories
  CHECK(std::filesystem::exists(path));

  auto loaded = loadPNG(path);
  REQUIRE(loaded.valid());
  CHECK(loaded.width == image.width);
  CHECK(loaded.height == image.height);
  CHECK(loaded.rgba == image.rgba);

  CHECK_FALSE(loadPNG((dir / "does_not_exist.png").string()).valid());

  std::filesystem::remove_all(dir.parent_path(), ec);
}
