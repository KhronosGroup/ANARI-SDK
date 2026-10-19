// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Image files for the pathtracer's texture system
// (renderer/pathtracer/ImageFile.*, HalcyonTextureSystem::Sample2D()):
//
// - PNG, JPEG, TGA, BMP, Radiance HDR, binary and ASCII PNM (8 and 16 bits)
//   and OpenEXR read, as stored (8 and 16 bits over their maximum), the
//   bottom row first, grey as (g, g, g), alpha only where the file has it;
// - a box-filtered mip chain down to 1 x 1;
// - Sample2D(): texel centres exactly, t flipped (lower-left origin), the
//   request's address modes and filter, the mip level of its derivatives,
//   the fill value for a missing alpha, sRGB colour data converted into the
//   working space (not other data), and a file the material couldn't read an
//   Error with the default value;
// - the device's cache: one image per file, re-read when the file changes,
//   a failure reported once.

#include "renderer/pathtracer/ColorManagement.h"
#include "renderer/pathtracer/HalcyonTextureSystem.h"
#include "renderer/pathtracer/ImageFile.h"
// stb_image_write for the test's files (external/images fetches stb)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
// tinyexr's writer (ImageFile.cpp, in halcyon_path_materials, implements it)
#include <tinyexr.h>
// std
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
// POSIX
#include <unistd.h>
#include <vector>

using namespace halcyon::pathtracer;
using mxcpp::Texture2DRequest;
using mxcpp::TextureAddressMode;
using mxcpp::TextureSampleStatus;
using mxcpp::Vec2f;
using mxcpp::Vec4f;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  if (!ok)
    g_failures++;
}

fs::path g_dir;

std::string file(const std::string &name)
{
  return (g_dir / name).string();
}

// 2 x 2 RGBA pixels, top row first as files store them: top-left red,
// top-right green, bottom-left blue, bottom-right white (alpha 255, 128,
// 64, 0)
const unsigned char RGBA[16] = {
    255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 255, 255, 255, 0};

// Texel (x, y) of level 'l', y from the bottom
Vec4f texel(const ImageFile &image, int x, int y, int l = 0)
{
  const ImageLevel &lv = image.levels[l];
  const float *t = lv.texels.data() + 4 * (size_t(y) * lv.width + x);
  return Vec4f(t[0], t[1], t[2], t[3]);
}

bool near(const Vec4f &a, const Vec4f &b, float eps = 1e-5f)
{
  for (int c = 0; c < 4; c++) {
    if (!(std::abs(a[c] - b[c]) <= eps)) {
      std::printf("    got (%g, %g, %g, %g), expected (%g, %g, %g, %g)\n",
          a[0],
          a[1],
          a[2],
          a[3],
          b[0],
          b[1],
          b[2],
          b[3]);
      return false;
    }
  }
  return true;
}

std::shared_ptr<const ImageFile> read(const std::string &path)
{
  std::string error;
  auto image = readImageFile(path, error);
  if (!image)
    std::printf("    %s: %s\n", path.c_str(), error.c_str());
  return image;
}

// The layout every reader must give the 2 x 2 image (RGB(A) as 'rgba'
// with alpha 'alpha' where the file has none)
bool layout(const ImageFile &image, bool alpha, float eps)
{
  const float a = 1.f;
  return image.levels.size() == 2 && image.levels[0].width == 2
      && image.levels[0].height == 2
      && image.hasAlpha == alpha
      // the bottom row first: blue, white
      && near(texel(image, 0, 0),
          Vec4f(0.f, 0.f, 1.f, alpha ? 64.f / 255.f : a),
          eps)
      && near(texel(image, 1, 0), Vec4f(1.f, 1.f, 1.f, alpha ? 0.f : a), eps)
      && near(texel(image, 0, 1), Vec4f(1.f, 0.f, 0.f, alpha ? 1.f : a), eps)
      && near(texel(image, 1, 1),
          Vec4f(0.f, 1.f, 0.f, alpha ? 128.f / 255.f : a),
          eps);
}

void writeBinary(const std::string &path, const std::string &data)
{
  std::ofstream out(path, std::ios::binary);
  out << data;
}

void testFormats()
{
  unsigned char rgb[12];
  for (int i = 0; i < 4; i++)
    for (int c = 0; c < 3; c++)
      rgb[3 * i + c] = RGBA[4 * i + c];

  stbi_write_png(file("rgba.png").c_str(), 2, 2, 4, RGBA, 8);
  stbi_write_png(file("rgb.png").c_str(), 2, 2, 3, rgb, 6);
  stbi_write_tga(file("rgba.tga").c_str(), 2, 2, 4, RGBA);
  stbi_write_bmp(file("rgb.bmp").c_str(), 2, 2, 3, rgb);
  auto png = read(file("rgba.png"));
  check(png && layout(*png, true, 1e-6f),
      "PNG with alpha: as stored, the bottom row first");
  auto png3 = read(file("rgb.png"));
  check(png3 && layout(*png3, false, 1e-6f), "PNG without alpha");
  auto tga = read(file("rgba.tga"));
  check(tga && layout(*tga, true, 1e-6f), "TGA");
  auto bmp = read(file("rgb.bmp"));
  check(bmp && layout(*bmp, false, 1e-6f), "BMP");

  // JPEG is lossy: a flat grey
  unsigned char grey[16 * 16 * 3];
  for (unsigned char &g : grey)
    g = 128;
  stbi_write_jpg(file("grey.jpg").c_str(), 16, 16, 3, grey, 100);
  auto jpg = read(file("grey.jpg"));
  check(jpg && jpg->levels.size() == 5 && !jpg->hasAlpha
          && near(texel(*jpg, 5, 7),
              Vec4f(128.f / 255.f, 128.f / 255.f, 128.f / 255.f, 1.f),
              2.f / 255.f),
      "JPEG, and a mip chain down to 1 x 1");

  // Radiance HDR: floats, beyond 1
  float hdr[12];
  for (int i = 0; i < 4; i++)
    for (int c = 0; c < 3; c++)
      hdr[3 * i + c] = RGBA[4 * i + c] / 255.f * 4.f;
  stbi_write_hdr(file("rgb.hdr").c_str(), 2, 2, 3, hdr);
  auto radiance = read(file("rgb.hdr"));
  check(radiance && radiance->levels[0].width == 2
          && near(texel(*radiance, 1, 0), Vec4f(4.f, 4.f, 4.f, 1.f), 0.05f)
          && near(texel(*radiance, 0, 1), Vec4f(4.f, 0.f, 0.f, 1.f), 0.05f),
      "Radiance HDR: floats as stored");

  // OpenEXR, with alpha
  float exr[16];
  for (int i = 0; i < 16; i++)
    exr[i] = RGBA[i] / 255.f;
  const char *err = nullptr;
  const int saved = SaveEXR(exr, 2, 2, 4, 0, file("rgba.exr").c_str(), &err);
  if (saved != TINYEXR_SUCCESS && err) {
    std::printf("    SaveEXR: %s\n", err);
    FreeEXRErrorMessage(err);
  }
  auto openexr = read(file("rgba.exr"));
  check(openexr && layout(*openexr, true, 1e-6f), "OpenEXR with alpha");
  const int savedRgb = SaveEXR(exr, 2, 2, 3, 0, file("rgb.exr").c_str(), &err);
  (void)savedRgb;
  auto openexr3 = read(file("rgb.exr"));
  check(openexr3 && !openexr3->hasAlpha, "OpenEXR without alpha");

  // binary PPM (8 and 16 bits) and PGM
  writeBinary(file("rgb.ppm"),
      "P6\n2 2\n255\n" + std::string(reinterpret_cast<char *>(rgb), 12));
  auto ppm = read(file("rgb.ppm"));
  check(ppm && layout(*ppm, false, 1e-6f), "binary PPM");
  std::string wide = "P6\n2 2\n65535\n";
  for (unsigned char v : rgb) {
    const unsigned short w = v * 257;
    wide.push_back(char(w >> 8));
    wide.push_back(char(w & 0xff));
  }
  writeBinary(file("rgb16.ppm"), wide);
  auto ppm16 = read(file("rgb16.ppm"));
  check(ppm16 && layout(*ppm16, false, 1e-6f), "16-bit binary PPM");

  // ASCII PPM and PGM (Typhoon's colour-management test images are P3)
  writeBinary(file("ascii.ppm"),
      "P3\n# a comment\n2 2\n255\n255 0 0  0 255 0\n0 0 255 255 255 255\n");
  auto ascii = read(file("ascii.ppm"));
  check(ascii && layout(*ascii, false, 1e-6f), "ASCII PPM (P3)");
  writeBinary(file("ascii.pgm"), "P2 2 1 10\n 5 10\n");
  auto pgm = read(file("ascii.pgm"));
  check(pgm && near(texel(*pgm, 0, 0), Vec4f(0.5f, 0.5f, 0.5f, 1.f))
          && near(texel(*pgm, 1, 0), Vec4f(1.f, 1.f, 1.f, 1.f)),
      "ASCII PGM (P2): grey as (g, g, g)");

  // mips: the 2 x 2 image's next level is the mean
  check(png
          && near(texel(*png, 0, 0, 1),
              Vec4f(0.5f, 0.5f, 0.5f, (255.f + 128.f + 64.f) / 4.f / 255.f)),
      "each mip level is the mean of the level above");

  std::string error;
  check(!readImageFile(file("missing.png"), error) && !error.empty(),
      "a missing file doesn't read, and says why");
  writeBinary(file("junk.png"), "not an image");
  check(!readImageFile(file("junk.png"), error) && !error.empty(),
      "a file that isn't an image doesn't read");
  writeBinary(file("short.ppm"), "P3 2 2 255 1 2 3");
  check(!readImageFile(file("short.ppm"), error) && !error.empty(),
      "a short ASCII PPM doesn't read");
}

Texture2DRequest request(const std::string &path, float s, float t)
{
  Texture2DRequest r;
  r.filePath = path;
  r.st = Vec2f(s, t);
  r.defaultValue = Vec4f(0.25f);
  r.filterType = mxcpp::TextureFilterType::Linear;
  r.channelCount = 4;
  r.channelFillValue = 0.75f;
  return r;
}

void testSampling()
{
  const std::string path = file("rgba.png");
  const std::string rgbPath = file("rgb.png");
  ImageFiles images;
  std::string error;
  images[path] = readImageFile(path, error);
  images[rgbPath] = readImageFile(rgbPath, error);
  const HalcyonTextureSystem textures({}, images);

  const float a1 = 1.f, a2 = 128.f / 255.f, a3 = 64.f / 255.f;
  auto r = textures.Sample2D(request(path, 0.25f, 0.75f));
  bool ok = r.status == TextureSampleStatus::Ok
      && near(r.value, Vec4f(1.f, 0.f, 0.f, a1));
  ok = ok
      && near(textures.Sample2D(request(path, 0.75f, 0.75f)).value,
          Vec4f(0.f, 1.f, 0.f, a2))
      && near(textures.Sample2D(request(path, 0.25f, 0.25f)).value,
          Vec4f(0.f, 0.f, 1.f, a3));
  check(ok,
      "Sample2D: texel centres exactly, t from the bottom (the file's top "
      "row at t near 1)");
  check(near(textures.Sample2D(request(path, 0.5f, 0.75f)).value,
            Vec4f(0.5f, 0.5f, 0.f, (a1 + a2) / 2.f)),
      "linear filtering between texels");

  auto closest = request(path, 0.45f, 0.75f);
  closest.filterType = mxcpp::TextureFilterType::Closest;
  check(near(textures.Sample2D(closest).value, Vec4f(1.f, 0.f, 0.f, a1)),
      "Closest takes the nearest texel");

  // address modes, at s = 1.25 (a quarter past the right edge)
  auto mode = [&](TextureAddressMode m) {
    auto req = request(path, 1.25f, 0.75f);
    req.uAddressMode = m;
    return textures.Sample2D(req).value;
  };
  check(near(mode(TextureAddressMode::Periodic), Vec4f(1.f, 0.f, 0.f, a1))
          && near(mode(TextureAddressMode::Clamp), Vec4f(0.f, 1.f, 0.f, a2))
          && near(mode(TextureAddressMode::Mirror), Vec4f(0.f, 1.f, 0.f, a2))
          && near(mode(TextureAddressMode::Constant), Vec4f(0.25f))
          && near(mode(TextureAddressMode::UseMetadata), Vec4f(0.25f)),
      "address modes: periodic, clamp, mirror; constant and useMetadata "
      "give the default outside [0, 1]");

  // a footprint of two texels: the next level, the mean
  auto wide = request(path, 0.25f, 0.75f);
  wide.dstdx = Vec2f(1.f, 0.f);
  check(near(textures.Sample2D(wide).value,
            Vec4f(0.5f, 0.5f, 0.5f, (a1 + a2 + a3) / 4.f)),
      "the request's derivatives pick the mip level");
  wide.dstdx = Vec2f(0.75f, 0.f); // 1.5 texels: halfway between levels
  const Vec4f level0(1.f, 0.f, 0.f, a1);
  const Vec4f level1(0.5f, 0.5f, 0.5f, (a1 + a2 + a3) / 4.f);
  const float f = std::log2(1.5f);
  check(near(textures.Sample2D(wide).value, level0 + (level1 - level0) * f),
      "trilinear between two levels");

  check(near(textures.Sample2D(request(rgbPath, 0.25f, 0.75f)).value,
            Vec4f(1.f, 0.f, 0.f, 0.75f)),
      "without alpha in the file, alpha is the request's fill value");

  // colour management, after filtering
  auto srgb = request(path, 0.5f, 0.75f);
  srgb.dataRole = mxcpp::TextureDataRole::Color;
  srgb.sourceColorSpace = "srgb_rec709_scene";
  mxcpp::Vec3f expected(0.5f, 0.5f, 0.f);
  convertToWorkingSpace("srgb_rec709_scene", expected);
  check(near(textures.Sample2D(srgb).value,
            Vec4f(expected[0], expected[1], expected[2], (a1 + a2) / 2.f))
          && expected[0] < 0.25f,
      "colour data converts from its colour space after filtering");
  srgb.dataRole = mxcpp::TextureDataRole::NonColor;
  check(near(textures.Sample2D(srgb).value,
            Vec4f(0.5f, 0.5f, 0.f, (a1 + a2) / 2.f)),
      "other data doesn't");

  auto missing = textures.Sample2D(request(file("missing.png"), 0.5f, 0.5f));
  check(missing.status == TextureSampleStatus::Error
          && near(missing.value, Vec4f(0.25f)),
      "a file the material didn't read is an Error, with the default value");

  mxcpp::MaterialGraph graph;
  graph.nodes["a"].parameters["file"] = mxcpp::Value(path);
  graph.nodes["b"].parameters["file"] = mxcpp::Value(path);
  graph.nodes["c"].parameters["file"] =
      mxcpp::Value(std::string("anari:sampler/0"));
  graph.nodes["d"].parameters["in"] = mxcpp::Value(std::string("x.png"));
  check(imageFiles(graph) == std::vector<std::string>{path},
      "imageFiles(): the graph's file parameters, once each, not sampler "
      "keys");
}

void testCache()
{
  ImageFileCache cache;
  const std::string path = file("cached.png");
  stbi_write_png(path.c_str(), 2, 2, 4, RGBA, 8);
  std::string error;
  auto a = cache.load(path, &error);
  auto b = cache.load(path, &error);
  check(a && a == b && error.empty(), "the cache reads a file once");

  // a new modification time reads it again
  unsigned char grey[4] = {9, 9, 9, 9};
  stbi_write_png(path.c_str(), 2, 2, 1, grey, 2);
  fs::last_write_time(
      path, fs::last_write_time(path) + std::chrono::seconds(2));
  auto c = cache.load(path, &error);
  check(c && c != a && !c->hasAlpha, "a changed file is read again");

  error.clear();
  const std::string missing = file("nope.png");
  auto m1 = cache.load(missing, &error);
  const std::string first = error;
  error.clear();
  auto m2 = cache.load(missing, &error);
  check(!m1 && !m2 && !first.empty() && error.empty(),
      "a file that doesn't read is reported once");
}

} // namespace

int main()
{
  std::error_code ec;
  g_dir = fs::temp_directory_path(ec)
      / ("halcyon_test_image_files_" + std::to_string(::getpid()));
  fs::create_directories(g_dir, ec);
  testFormats();
  testSampling();
  testCache();
  fs::remove_all(g_dir, ec);
  return g_failures == 0 ? 0 : 1;
}
