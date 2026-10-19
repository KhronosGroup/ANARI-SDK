// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// The C++ reference here ports helide's image sampling (ANARI-SDK,
// src/devices/helide/sampler/), Copyright 2021-2026 The Khronos Group,
// licensed under Apache-2.0.

// Renders a quad facing an orthographic camera (so eye-lit shading gives the
// surface color exactly) whose matte color comes from an image sampler, and
// compares every pixel on the quad against a C++ port of helide's image
// sampling: 1D/2D/3D images, filters, wrap modes, in/out transforms and
// offsets. Also covers opacity samplers, invalid samplers and updates, and
// the transform and primitive samplers.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr uint2 IMAGE_SIZE = {37, 37};
// device warnings, in order, recorded by statusFunc(); they don't fail the
// test
std::vector<std::string> g_warnings;
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};
// matte's default colour (the specification's), which invalid samplers
// fall back to
const float4 MATTE_DEFAULT = {0.8f, 0.8f, 0.8f, 1.f};

// The quad covers [-0.8, 0.8]^2 at z = 0 facing +z; 'attribute0' is
// (u, v, 0.3, 1) with (u, v) going from (0, 0) to (1, 1) across it
constexpr float QUAD_HALF_SIZE = 0.8f;
constexpr float ATTRIBUTE0_Z = 0.3f;

// Image sampler description, also used by the reference implementation
struct ImageDesc
{
  int dimensions{2};
  uint3 size{1, 1, 1};
  std::vector<float4> texels; // x fastest
  const char *filter{"linear"};
  const char *wrapMode[3]{"clampToEdge", "clampToEdge", "clampToEdge"};
  const char *inAttribute{"attribute0"};
  mat4 inTransform{linalg::identity};
  float4 inOffset{0.f};
  mat4 outTransform{linalg::identity};
  float4 outOffset{0.f};
};

// Reference sampling (helide's Image1D/2D/3D::getSample()) ///////////////////

int wrapIndex(int i, int size, const std::string &wrap)
{
  if (wrap == "repeat") {
    const int r = i % size;
    return r < 0 ? r + size : r;
  } else if (wrap == "mirrorRepeat") {
    const int x = std::abs(i + (i < 0)) % (2 * size);
    return x >= size ? 2 * size - x - 1 : x;
  }
  return std::clamp(i, 0, size - 1);
}

struct Interpolant
{
  int lower;
  int upper;
  float frac;
};

Interpolant interpolant(float coord, int size)
{
  const float lowf = (coord - 0.5f / size) * size;
  const int low = int(std::floor(lowf));
  return {low, low + 1, lowf - low};
}

// Whether the sample at 'in' is too close to a discontinuity (a 'repeat'
// wrap, a 'nearest' switch) for float differences to be ignored
bool nearDiscontinuity(const ImageDesc &img, float4 in)
{
  const float c[3] = {in.x, in.y, in.z};
  for (int d = 0; d < img.dimensions; d++) {
    const float f = c[d] - std::floor(c[d]);
    if (std::string(img.wrapMode[d]) == "repeat"
        && (f < 1e-3f || f > 1 - 1e-3f))
      return true;
    const float x = std::string(img.wrapMode[d]) == "repeat" ? f : c[d];
    const auto i = interpolant(x, img.size[d]);
    if (std::abs(i.frac - 0.5f) < 1e-3f || i.frac < 1e-3f || i.frac > 1 - 1e-3f)
      return true;
  }
  return false;
}

// helide's sample for attribute0 value 'a'; 'skip' is set near
// discontinuities
float4 referenceSample(const ImageDesc &img, float4 a, bool &skip)
{
  skip = false;
  if (std::string(img.inAttribute) == "none")
    return float4(0.f, 0.f, 0.f, 1.f);

  const float4 in = linalg::mul(img.inTransform, a) + img.inOffset;
  skip = nearDiscontinuity(img, in);
  const bool linear = std::string(img.filter) != "nearest";
  const float c[3] = {in.x, in.y, in.z};

  Interpolant ip[3] = {{0, 0, 0.f}, {0, 0, 0.f}, {0, 0, 0.f}};
  for (int d = 0; d < img.dimensions; d++) {
    const bool repeat = std::string(img.wrapMode[d]) == "repeat";
    ip[d] = interpolant(repeat ? c[d] - std::floor(c[d]) : c[d], img.size[d]);
  }
  auto texel = [&](int x, int y, int z) {
    x = wrapIndex(x, img.size.x, img.wrapMode[0]);
    y = wrapIndex(y, img.size.y, img.wrapMode[1]);
    z = wrapIndex(z, img.size.z, img.wrapMode[2]);
    return img.texels[x + size_t(img.size.x) * (y + size_t(img.size.y) * z)];
  };
  auto filter = [&](float4 v0, float4 v1, float f) {
    return linear ? linalg::lerp(v0, v1, f) : (f < 0.5f ? v0 : v1);
  };

  // filter along z, then y, then x; unused dimensions have one texel
  float4 v[2][2];
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      const int x = i ? ip[0].upper : ip[0].lower;
      const int y = j ? ip[1].upper : ip[1].lower;
      v[i][j] = filter(
          texel(x, y, ip[2].lower), texel(x, y, ip[2].upper), ip[2].frac);
    }
  }
  const float4 r = filter(filter(v[0][0], v[0][1], ip[1].frac),
      filter(v[1][0], v[1][1], ip[1].frac),
      ip[0].frac);
  return linalg::mul(img.outTransform, r) + img.outOffset;
}

// Scene setup ////////////////////////////////////////////////////////////////

// Sets 'img' as sampler 's''s 'image' (typed handles, so the parameter has
// the right ANARI_ARRAY<N>D type)
void setImage(anari::Device d, anari::Sampler s, const ImageDesc &img)
{
  auto fill = [&](auto a) {
    auto *p = anari::map<float4>(d, a);
    std::memcpy(p, img.texels.data(), img.texels.size() * sizeof(float4));
    anari::unmap(d, a);
    anari::setAndReleaseParameter(d, s, "image", a);
  };
  if (img.dimensions == 1)
    fill(anari::newArray1D(d, ANARI_FLOAT32_VEC4, img.size.x));
  else if (img.dimensions == 2)
    fill(anari::newArray2D(d, ANARI_FLOAT32_VEC4, img.size.x, img.size.y));
  else {
    fill(anari::newArray3D(
        d, ANARI_FLOAT32_VEC4, img.size.x, img.size.y, img.size.z));
  }
}

anari::Sampler newImageSampler(anari::Device d, const ImageDesc &img)
{
  const char *subtype = img.dimensions == 1 ? "image1D"
      : img.dimensions == 2                 ? "image2D"
                                            : "image3D";
  auto s = anari::newObject<anari::Sampler>(d, subtype);
  setImage(d, s, img);
  anari::setParameter(d, s, "filter", img.filter);
  if (img.dimensions == 1) {
    anari::setParameter(d, s, "wrapMode", img.wrapMode[0]);
  } else {
    const char *names[3] = {"wrapMode1", "wrapMode2", "wrapMode3"};
    for (int i = 0; i < img.dimensions; i++)
      anari::setParameter(d, s, names[i], img.wrapMode[i]);
  }
  anari::setParameter(d, s, "inAttribute", img.inAttribute);
  anari::setParameter(d, s, "inTransform", img.inTransform);
  anari::setParameter(d, s, "inOffset", img.inOffset);
  anari::setParameter(d, s, "outTransform", img.outTransform);
  anari::setParameter(d, s, "outOffset", img.outOffset);
  anari::commitParameters(d, s);
  return s;
}

// A world with the quad and a matte material; 'material' sets it up
anari::World makeWorld(anari::Device d,
    const std::function<void(anari::Material)> &material,
    anari::Material *materialOut = nullptr,
    const std::function<void(anari::Geometry)> &geometry = {})
{
  const float h = QUAD_HALF_SIZE;
  const float3 vertices[] = {
      {-h, -h, 0.f}, {h, -h, 0.f}, {h, h, 0.f}, {-h, h, 0.f}};
  const float4 uvs[] = {{0.f, 0.f, ATTRIBUTE0_Z, 1.f},
      {1.f, 0.f, ATTRIBUTE0_Z, 1.f},
      {1.f, 1.f, ATTRIBUTE0_Z, 1.f},
      {0.f, 1.f, ATTRIBUTE0_Z, 1.f}};
  const uint3 indices[] = {{0, 1, 2}, {0, 2, 3}};

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", vertices, 4);
  anari::setParameterArray1D(d, geom, "vertex.attribute0", uvs, 4);
  anari::setParameterArray1D(d, geom, "primitive.index", indices, 2);
  if (geometry)
    geometry(geom);
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  material(mat);
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  if (materialOut)
    *materialOut = mat;
  else
    anari::release(d, mat);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  anari::release(d, surface);
  return world;
}

// Renders 'world' in renderer 'mode' and returns the float color channel
std::vector<float4> render(
    anari::Device d, anari::World world, const char *mode)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "mode", mode);
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);

  std::vector<float4> pixels;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
    pixels.assign(fb.data, fb.data + fb.width * fb.height);
  else
    std::printf("FAIL %s: could not map channel.color\n", mode);
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);
  return pixels;
}

bool near(float4 a, float4 b)
{
  const float4 d = linalg::abs(a - b);
  return d.x < 1e-4f && d.y < 1e-4f && d.z < 1e-4f && d.w < 1e-4f;
}

// Calls 'f(pixel, attribute0)' for every pixel well inside the quad (the
// orthographic camera's ray for pixel (x, y) starts at the pixel's centre)
void forEachQuadPixel(const std::function<void(size_t, float4)> &f)
{
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      const float2 p(2.f * (x + 0.5f) / IMAGE_SIZE.x - 1.f,
          2.f * (y + 0.5f) / IMAGE_SIZE.y - 1.f);
      if (std::abs(p.x) > 0.75f || std::abs(p.y) > 0.75f)
        continue;
      const float2 uv = (p + QUAD_HALF_SIZE) / (2.f * QUAD_HALF_SIZE);
      f(size_t(y) * IMAGE_SIZE.x + x, float4(uv, ATTRIBUTE0_Z, 1.f));
    }
  }
}

// Compares every pixel well inside the quad to the rgb of
// 'expected(attribute0)' (alpha 1); pixels it returns no value for are skipped
bool comparePixels(const char *test,
    const std::vector<float4> &pixels,
    const std::function<std::optional<float4>(float4)> &expected)
{
  if (pixels.empty())
    return false;

  int checked = 0, failed = 0;
  forEachQuadPixel([&](size_t i, float4 a) {
    const auto r = expected(a);
    if (!r)
      return;
    const float4 e(r->x, r->y, r->z, 1.f);
    checked++;
    if (near(pixels[i], e))
      return;
    if (failed++ == 0) {
      std::printf(
          "FAIL %s: attribute0 (%f, %f): got (%f, %f, %f), expected "
          "(%f, %f, %f)\n",
          test,
          a.x,
          a.y,
          pixels[i].x,
          pixels[i].y,
          pixels[i].z,
          e.x,
          e.y,
          e.z);
    }
  });
  // most pixels must be comparable for the test to mean anything
  const bool pass = failed == 0 && checked > 400;
  std::printf("%s %s (%d pixels, %d failed)\n",
      pass ? "PASS" : "FAIL",
      test,
      checked,
      failed);
  return pass;
}

// Renders the quad with color sampler 'img' and compares every pixel on it to
// the reference (with the eye-lit shading, the pixel is the sample's rgb)
bool checkImage(anari::Device d, const char *test, const ImageDesc &img)
{
  auto sampler = newImageSampler(d, img);
  auto world = makeWorld(d,
      [&](anari::Material m) { anari::setParameter(d, m, "color", sampler); });
  anari::release(d, sampler);
  const auto pixels = render(d, world, "default");
  anari::release(d, world);
  return comparePixels(test, pixels, [&](float4 a) -> std::optional<float4> {
    bool skip = false;
    const float4 r = referenceSample(img, a, skip);
    return skip ? std::nullopt : std::optional<float4>(r);
  });
}

// Deterministic, distinct texel colors
std::vector<float4> makeTexels(size_t n)
{
  std::vector<float4> t(n);
  for (size_t i = 0; i < n; i++) {
    t[i] = float4(std::fmod(0.37f * i + 0.1f, 1.f),
        std::fmod(0.61f * i + 0.5f, 1.f),
        std::fmod(0.13f * i + 0.8f, 1.f),
        1.f);
  }
  return t;
}

ImageDesc image(int dimensions, uint3 size)
{
  ImageDesc img;
  img.dimensions = dimensions;
  img.size = size;
  img.texels = makeTexels(size_t(size.x) * size.y * size.z);
  return img;
}

bool testImages(anari::Device d)
{
  bool pass = true;

  for (const char *filter : {"linear", "nearest"}) {
    auto img1 = image(1, {5, 1, 1});
    img1.filter = filter;
    pass &= checkImage(d, (std::string("image1D ") + filter).c_str(), img1);

    auto img2 = image(2, {4, 3, 1});
    img2.filter = filter;
    pass &= checkImage(d, (std::string("image2D ") + filter).c_str(), img2);

    auto img3 = image(3, {3, 4, 2});
    img3.filter = filter;
    pass &= checkImage(d, (std::string("image3D ") + filter).c_str(), img3);
  }

  // wrap modes, with coordinates spanning [-1.5, 2.5] (and a
  // non-power-of-two size, where helium's 'repeat' differs)
  mat4 scale = linalg::scaling_matrix(float3(4.f, 4.f, 4.f));
  const float4 shift(-1.5f, -1.5f, -1.f, 0.f);
  for (const char *wrap : {"clampToEdge", "repeat", "mirrorRepeat"}) {
    for (const char *filter : {"linear", "nearest"}) {
      const std::string suffix = std::string(wrap) + " " + filter;

      auto img1 = image(1, {3, 1, 1});
      img1.filter = filter;
      img1.wrapMode[0] = wrap;
      img1.inTransform = scale;
      img1.inOffset = shift;
      pass &= checkImage(d, ("image1D " + suffix).c_str(), img1);

      auto img2 = image(2, {3, 5, 1});
      img2.filter = filter;
      img2.wrapMode[0] = wrap;
      img2.wrapMode[1] = wrap;
      img2.inTransform = scale;
      img2.inOffset = shift;
      pass &= checkImage(d, ("image2D " + suffix).c_str(), img2);

      auto img3 = image(3, {3, 2, 3});
      img3.filter = filter;
      for (auto &w : img3.wrapMode)
        w = wrap;
      img3.inTransform = scale;
      img3.inOffset = shift;
      pass &= checkImage(d, ("image3D " + suffix).c_str(), img3);
    }
  }

  // wrap modes per dimension
  auto mixed = image(3, {3, 3, 3});
  mixed.wrapMode[0] = "repeat";
  mixed.wrapMode[1] = "mirrorRepeat";
  mixed.wrapMode[2] = "clampToEdge";
  mixed.inTransform = scale;
  mixed.inOffset = shift;
  pass &= checkImage(d, "image3D mixed wrap modes", mixed);

  // in/out transforms and offsets (image3D included, unlike helide)
  const mat4 swizzle = {{0.f, 1.f, 0.f, 0.f},
      {1.f, 0.f, 0.f, 0.f},
      {0.f, 0.f, 1.f, 0.f},
      {0.f, 0.f, 0.f, 1.f}};
  const mat4 outXfm = {{0.5f, 0.f, 0.f, 0.f},
      {0.f, 0.f, 0.25f, 0.f},
      {0.f, 0.5f, 0.f, 0.f},
      {0.f, 0.f, 0.f, 1.f}};
  for (int dims = 1; dims <= 3; dims++) {
    auto img = image(dims, {4, 3, 2});
    if (dims < 3)
      img.size.z = 1;
    if (dims < 2)
      img.size.y = 1;
    img.texels = makeTexels(size_t(img.size.x) * img.size.y * img.size.z);
    img.inTransform = swizzle;
    img.inOffset = float4(0.1f, -0.2f, 0.3f, 0.f);
    img.outTransform = outXfm;
    img.outOffset = float4(0.2f, 0.1f, 0.05f, 0.f);
    const std::string test = "image" + std::to_string(dims) + "D transforms";
    pass &= checkImage(d, test.c_str(), img);
  }

  // other input attributes; "none" samples to (0, 0, 0, 1)
  pass &= checkImage(d, "image2D inAttribute none", [&] {
    auto img = image(2, {4, 4, 1});
    img.inAttribute = "none";
    return img;
  }());

  return pass;
}

// Renders 'world' in 'mode' and checks the pixel at the quad's center
bool checkCenter(anari::Device d,
    const char *test,
    anari::World world,
    const char *mode,
    float4 expected)
{
  const auto pixels = render(d, world, mode);
  if (pixels.empty())
    return false;
  const size_t i = size_t(IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + IMAGE_SIZE.x / 2;
  const bool pass = near(pixels[i], expected);
  if (!pass) {
    std::printf("FAIL %s: got (%f, %f, %f, %f), expected (%f, %f, %f, %f)\n",
        test,
        pixels[i].x,
        pixels[i].y,
        pixels[i].z,
        pixels[i].w,
        expected.x,
        expected.y,
        expected.z,
        expected.w);
  }
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

// helide's opacity heatmap (blue, red, yellow, white), face-on
float3 heatmap(float o)
{
  const float3 stops[] = {
      {0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, {1.f, 1.f, 0.f}, {1.f, 1.f, 1.f}};
  const float f = linalg::clamp(o, 0.f, 1.f) * 3.f;
  const int i = f >= 3.f ? 2 : int(f);
  return linalg::lerp(stops[i], stops[i + 1], f - i);
}

// A 1x1 image sampler of 'value'
anari::Sampler constantSampler(anari::Device d, float4 value)
{
  ImageDesc img;
  img.dimensions = 2;
  img.size = {1, 1, 1};
  img.texels = {value};
  return newImageSampler(d, img);
}

bool testMaterialUse(anari::Device d)
{
  bool pass = true;
  const float4 value(0.25f, 0.5f, 0.75f, 0.5f);

  // the pixel is opaque whatever the sample's alpha
  {
    auto s = constantSampler(d, value);
    auto world = makeWorld(
        d, [&](anari::Material m) { anari::setParameter(d, m, "color", s); });
    pass &= checkCenter(d,
        "matte color sampler",
        world,
        "default",
        float4(0.25f, 0.5f, 0.75f, 1.f));
    anari::release(d, world);
    anari::release(d, s);
  }

  // opacity samplers use the sample's x; the color sampler's alpha scales it
  {
    auto s = constantSampler(d, float4(0.6f, 0.f, 0.f, 1.f));
    auto world = makeWorld(d, [&](anari::Material m) {
      anari::setParameter(d, m, "opacity", s);
      anari::setParameter(d, m, "alphaMode", "blend");
    });
    pass &= checkCenter(d,
        "opacity sampler",
        world,
        "opacityHeatmap",
        float4(heatmap(0.6f), 1.f));
    anari::release(d, world);

    auto c = constantSampler(d, value);
    world = makeWorld(d, [&](anari::Material m) {
      anari::setParameter(d, m, "color", c);
      anari::setParameter(d, m, "opacity", s);
      anari::setParameter(d, m, "alphaMode", "blend");
    });
    pass &= checkCenter(d,
        "opacity sampler times color sampler alpha",
        world,
        "opacityHeatmap",
        float4(heatmap(0.3f), 1.f));
    anari::release(d, world);
    anari::release(d, c);
    anari::release(d, s);
  }

  // invalid samplers fall back to the material's (default) color
  {
    auto noImage = anari::newObject<anari::Sampler>(d, "image2D");
    anari::commitParameters(d, noImage);
    auto world = makeWorld(d, [&](anari::Material m) {
      anari::setParameter(d, m, "color", noImage);
    });
    pass &= checkCenter(
        d, "sampler without image", world, "default", MATTE_DEFAULT);
    anari::release(d, world);
    anari::release(d, noImage);

    auto neverCommitted = anari::newObject<anari::Sampler>(d, "image1D");
    world = makeWorld(d, [&](anari::Material m) {
      anari::setParameter(d, m, "color", neverCommitted);
    });
    pass &= checkCenter(
        d, "sampler never committed", world, "default", MATTE_DEFAULT);
    anari::release(d, world);
    anari::release(d, neverCommitted);

    auto unknown = anari::newObject<anari::Sampler>(d, "no such sampler");
    anari::commitParameters(d, unknown);
    world = makeWorld(d, [&](anari::Material m) {
      anari::setParameter(d, m, "color", unknown);
    });
    pass &= checkCenter(
        d, "unknown sampler subtype", world, "default", MATTE_DEFAULT);
    anari::release(d, world);
    anari::release(d, unknown);

    // an image with a zero dimension makes the sampler invalid, with a
    // warning
    const uint3 emptySizes[] = {{0, 1, 1}, {2, 0, 1}, {2, 2, 0}};
    for (int dims = 1; dims <= 3; dims++) {
      ImageDesc img;
      img.dimensions = dims;
      img.size = emptySizes[dims - 1];
      const std::string test = "empty image" + std::to_string(dims) + "D";
      const size_t warnings = g_warnings.size();
      auto empty = newImageSampler(d, img);
      bool valid = true;
      anari::getProperty(d, empty, "valid", valid, ANARI_WAIT);
      const bool warned = std::any_of(g_warnings.begin() + warnings,
          g_warnings.end(),
          [](const std::string &w) { return w.find("empty") != w.npos; });
      std::printf("%s %s: invalid with a warning\n",
          !valid && warned ? "PASS" : "FAIL",
          test.c_str());
      pass &= !valid && warned;
      world = makeWorld(d, [&](anari::Material m) {
        anari::setParameter(d, m, "color", empty);
      });
      pass &= checkCenter(d, test.c_str(), world, "default", MATTE_DEFAULT);
      anari::release(d, world);
      anari::release(d, empty);
    }
  }

  // sampler updates reach materials that aren't committed again
  {
    auto s = constantSampler(d, value);
    auto world = makeWorld(
        d, [&](anari::Material m) { anari::setParameter(d, m, "color", s); });
    const float4 opaque(0.25f, 0.5f, 0.75f, 1.f);
    pass &= checkCenter(d, "update: initial", world, "default", opaque);

    ImageDesc red;
    red.size = {1, 1, 1};
    red.texels = {float4(1.f, 0.f, 0.f, 1.f)};
    setImage(d, s, red);
    anari::commitParameters(d, s);
    pass &= checkCenter(
        d, "update: new image", world, "default", float4(1.f, 0.f, 0.f, 1.f));

    anari::unsetParameter(d, s, "image");
    anari::commitParameters(d, s);
    pass &= checkCenter(
        d, "update: image removed", world, "default", MATTE_DEFAULT);
    anari::release(d, world);
    anari::release(d, s);
  }

  // images of other element types are read as attribute values
  {
    const uint8_t texel[4] = {255, 0, 51, 255};
    auto a = anari::newArray2D(d, ANARI_UFIXED8_VEC4, 1, 1);
    std::memcpy(anari::map<uint8_t>(d, a), texel, sizeof(texel));
    anari::unmap(d, a);
    auto s = anari::newObject<anari::Sampler>(d, "image2D");
    anari::setAndReleaseParameter(d, s, "image", a);
    anari::commitParameters(d, s);
    auto world = makeWorld(
        d, [&](anari::Material m) { anari::setParameter(d, m, "color", s); });
    pass &= checkCenter(
        d, "ufixed8 image", world, "default", float4(1.f, 0.f, 0.2f, 1.f));
    anari::release(d, world);
    anari::release(d, s);
  }

  return pass;
}

// Transform sampler /////////////////////////////////////////////////////////

// Transform sampler description; 'outTransform'/'outOffset' are the spec's
// names, 'transform' helide's name for the matrix, which halcyon doesn't read
// (unset when null)
struct TransformDesc
{
  const char *inAttribute{"attribute0"};
  const mat4 *outTransform{nullptr};
  const mat4 *transform{nullptr};
  const float4 *outOffset{nullptr};
};

// Renders the quad with a transform sampler as color and compares every pixel
// on it to 'expected(attribute0)'
bool checkTransform(anari::Device d,
    const char *test,
    const TransformDesc &desc,
    const std::function<float4(float4)> &expected)
{
  auto s = anari::newObject<anari::Sampler>(d, "transform");
  anari::setParameter(d, s, "inAttribute", desc.inAttribute);
  if (desc.outTransform)
    anari::setParameter(d, s, "outTransform", *desc.outTransform);
  if (desc.transform)
    anari::setParameter(d, s, "transform", *desc.transform);
  if (desc.outOffset)
    anari::setParameter(d, s, "outOffset", *desc.outOffset);
  anari::commitParameters(d, s);
  auto world = makeWorld(
      d, [&](anari::Material m) { anari::setParameter(d, m, "color", s); });
  anari::release(d, s);
  const auto pixels = render(d, world, "default");
  anari::release(d, world);
  return comparePixels(test, pixels, expected);
}

bool testTransformSampler(anari::Device d)
{
  bool pass = true;
  const mat4 m = {{0.f, 0.5f, 0.f, 0.f},
      {0.25f, 0.f, 0.5f, 0.f},
      {1.f, 0.f, 0.f, 0.f},
      {0.1f, 0.2f, 0.f, 1.f}};
  const float4 offset(0.05f, -0.1f, 0.2f, 0.f);

  TransformDesc identity;
  pass &= checkTransform(
      d, "transform identity", identity, [](float4 a) { return a; });

  TransformDesc spec;
  spec.outTransform = &m;
  spec.outOffset = &offset;
  pass &= checkTransform(
      d, "transform outTransform outOffset", spec, [&](float4 a) {
        return linalg::mul(m, a) + offset;
      });

  // helide's 'transform' name is not read
  TransformDesc helide;
  helide.transform = &m;
  pass &= checkTransform(d,
      "transform ignores helide name",
      helide,
      [](float4 a) { return a; });

  const mat4 other = linalg::scaling_matrix(float3(0.5f, 0.5f, 0.5f));
  TransformDesc both;
  both.outTransform = &other;
  both.transform = &m;
  pass &= checkTransform(d, "transform outTransform only", both, [&](float4 a) {
    return linalg::mul(other, a);
  });

  // "none" samples to (0, 0, 0, 1), untransformed as in helide
  TransformDesc none;
  none.inAttribute = "none";
  none.outOffset = &offset;
  pass &= checkTransform(d, "transform inAttribute none", none, [](float4) {
    return float4(0.f, 0.f, 0.f, 1.f);
  });

  return pass;
}

// Primitive sampler //////////////////////////////////////////////////////////

// Renders 'world' and checks each of the quad's triangles: primitive 0
// (u > v) must be 'prim0', primitive 1 'prim1'
bool checkPrimitives(anari::Device d,
    const char *test,
    anari::World world,
    float4 prim0,
    float4 prim1)
{
  const auto pixels = render(d, world, "default");
  return comparePixels(test, pixels, [&](float4 a) -> std::optional<float4> {
    if (std::abs(a.x - a.y) < 0.05f)
      return std::nullopt;
    return a.x > a.y ? prim0 : prim1;
  });
}

bool testPrimitiveSampler(anari::Device d)
{
  bool pass = true;
  const float4 values[] = {
      {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}, {0.f, 0.f, 1.f, 1.f}};

  auto newSampler = [&]() {
    auto s = anari::newObject<anari::Sampler>(d, "primitive");
    anari::setParameterArray1D(d, s, "array", values, 3);
    return s;
  };
  auto check = [&](const char *test,
                   anari::Sampler s,
                   float4 prim0,
                   float4 prim1,
                   const std::function<void(anari::Geometry)> &geometry = {}) {
    anari::commitParameters(d, s);
    auto world = makeWorld(
        d,
        [&](anari::Material m) { anari::setParameter(d, m, "color", s); },
        nullptr,
        geometry);
    const bool ok = checkPrimitives(d, test, world, prim0, prim1);
    anari::release(d, world);
    return ok;
  };

  auto s = newSampler();
  pass &= check("primitive", s, values[0], values[1]);

  // the spec's 'inOffset' (uint64); indices past the end clamp
  anari::setParameter(d, s, "inOffset", uint64_t(1));
  pass &= check("primitive inOffset", s, values[1], values[2]);
  anari::setParameter(d, s, "inOffset", uint64_t(2));
  pass &= check("primitive inOffset clamps", s, values[2], values[2]);
  anari::release(d, s);

  // helide's 'offset' name is not read
  s = newSampler();
  anari::setParameter(d, s, "offset", uint32_t(1));
  pass &= check("primitive ignores offset (helide name)", s, values[0], values[1]);
  anari::setParameter(d, s, "offset", uint64_t(1));
  pass &= check("primitive ignores uint64 offset", s, values[0], values[1]);
  anari::setParameter(d, s, "inOffset", uint64_t(2));
  pass &= check("primitive inOffset only", s, values[2], values[2]);
  anari::release(d, s);

  // indexed by the primitive index, not 'primitive.id' (as helide)
  s = newSampler();
  pass &= check("primitive ignores primitive.id",
      s,
      values[0],
      values[1],
      [&](anari::Geometry g) {
        const uint32_t ids[] = {2, 0};
        anari::setParameterArray1D(d, g, "primitive.id", ids, 2);
      });

  // array updates; a missing array makes the sampler invalid
  const float4 gray[] = {{0.5f, 0.5f, 0.5f, 1.f}};
  anari::setParameterArray1D(d, s, "array", gray, 1);
  pass &= check("primitive new array", s, gray[0], gray[0]);
  anari::unsetParameter(d, s, "array");
  pass &= check("primitive without array", s, MATTE_DEFAULT, MATTE_DEFAULT);
  anari::release(d, s);

  // other element types are read as attribute values
  s = anari::newObject<anari::Sampler>(d, "primitive");
  const float scalars[] = {0.25f, 0.75f};
  anari::setParameterArray1D(d, s, "array", scalars, 2);
  pass &= check("primitive float array",
      s,
      float4(0.25f, 0.f, 0.f, 1.f),
      float4(0.75f, 0.f, 0.f, 1.f));
  anari::release(d, s);

  return pass;
}

// device errors (e.g. ISPC object size mismatches) fail the test; warnings
// are recorded in g_warnings
bool g_deviceError = false;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR) {
    g_deviceError = true;
    std::fprintf(stderr, "[ANARI] %s\n", message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    g_warnings.push_back(message);
  }
}

} // namespace

int main()
{
  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  bool pass = true;
  pass &= testImages(d);
  pass &= testMaterialUse(d);
  pass &= testTransformSampler(d);
  pass &= testPrimitiveSampler(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceError)
    std::printf("FAIL: device reported errors\n");

  return pass && !g_deviceError ? 0 : 1;
}
