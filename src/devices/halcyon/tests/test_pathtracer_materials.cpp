// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// matte and physicallyBased through the 'quality' renderer, which shades
// them as MaterialXCpp graphs in C++ (renderer/pathtracer/MaterialConverter.*,
// PathMaterial.*): a floor seen from straight above under a directional light
// of irradiance pi straight down, with no bounces, so that a Lambertian floor
// shows its albedo exactly. Checks constant parameters, parameters from
// geometry attributes (per primitive) and ray-derived attributes, alpha
// modes, emission, a physicallyBased dielectric without specular reflection
// (a Lambertian of its base colour), mask cut-outs passed by every kind of
// ray, a normal map, and that 'occlusion' isn't used and says so in a DEBUG
// message. Sampler-sourced parameters (every subtype, wrap mode, filter and
// transform) are compared with the 'preview' renderer, whose colour is the
// surface colour in this view, and must follow a sampler committed again.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr float PI = 3.14159265358979323846f;
constexpr uint2 IMAGE_SIZE = {32, 32};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};

int g_errors = 0;
std::vector<std::string> g_debug;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR
      || severity == ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI %s] %s\n",
        severity == ANARI_SEVERITY_WARNING ? "WARNING" : "ERROR",
        message);
    g_errors++;
  } else if (severity == ANARI_SEVERITY_DEBUG) {
    g_debug.push_back(message);
  }
}

bool report(const std::string &name, bool ok, const std::string &detail = {})
{
  std::printf("%s %s%s%s\n",
      ok ? "PASS" : "FAIL",
      name.c_str(),
      detail.empty() ? "" : ": ",
      detail.c_str());
  return ok;
}

// Scene //////////////////////////////////////////////////////////////////////

using SetMaterial = std::function<void(anari::Device, anari::Material)>;
using SetGeometry = std::function<void(anari::Device, anari::Geometry)>;

anari::Material material(
    anari::Device d, const char *subtype, const SetMaterial &set)
{
  auto mat = anari::newObject<anari::Material>(d, subtype);
  set(d, mat);
  anari::commitParameters(d, mat);
  return mat;
}

// The floor [-1, 1]^2 at z = 0, facing +z: triangle 0 below the diagonal
// x = y (seen from above, x right and y up), triangle 1 above it
anari::World floorWorld(anari::Device d,
    anari::Material mat,
    bool lit = true,
    const SetGeometry &setGeometry = {})
{
  const std::vector<float3> v = {
      {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  if (setGeometry)
    setGeometry(d, geom);
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::release(d, surface);
  if (lit) {
    auto light = anari::newObject<anari::Light>(d, "directional");
    anari::setParameter(d, light, "direction", float3(0.f, 0.f, -1.f));
    anari::setParameter(d, light, "irradiance", PI);
    anari::commitParameters(d, light);
    anari::setParameterArray1D(d, world, "light", &light, 1);
    anari::release(d, light);
  }
  anari::commitParameters(d, world);
  return world;
}

// 'pixelSamples' samples per pixel through an orthographic camera looking
// down at the floor, no bounces after the first vertex; or with the 'preview'
// renderer (without ambient occlusion), whose colour is the surface colour
// there (falloff 1)
std::vector<float4> render(anari::Device d,
    anari::World world,
    int pixelSamples = 1,
    const char *subtype = "quality",
    bool nativeMaterials = true)
{
  auto renderer = anari::newObject<anari::Renderer>(d, subtype);
  if (!nativeMaterials)
    anari::setParameter(d, renderer, "nativeMaterials", false);
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "maxBounces", 0);
  anari::setParameter(d, renderer, "pixelSamples", pixelSamples);
  anari::setParameter(d, renderer, "ambientOcclusion", false);
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 3.f));
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
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);
  anari::release(d, world);
  return pixels;
}

// Which triangle pixel (x, y) shows, -1 for pixels the diagonal may cross
// (the camera jitters within the pixel)
int triangleOf(uint32_t x, uint32_t y)
{
  if (int(x) - int(y) >= 2)
    return 0;
  if (int(y) - int(x) >= 2)
    return 1;
  return -1;
}

// Whether every pixel of 'image' (off the diagonal) shows 'expected[t]' for
// triangle t, within 'tolerance'
bool matches(const std::string &name,
    const std::vector<float4> &image,
    const float3 expected[2],
    float tolerance = 1e-4f)
{
  if (image.empty())
    return report(name, false, "no image");
  float maxError = 0.f;
  int checked = 0;
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      const int t = triangleOf(x, y);
      if (t < 0)
        continue;
      const float4 p = image[size_t(y) * IMAGE_SIZE.x + x];
      for (int c = 0; c < 3; c++)
        maxError = std::max(maxError, std::abs(p[c] - expected[t][c]));
      checked++;
    }
  }
  char detail[64];
  std::snprintf(
      detail, sizeof(detail), "%d pixels, largest error %g", checked, maxError);
  return report(name, checked > 800 && maxError <= tolerance, detail);
}

bool matches(const std::string &name,
    const std::vector<float4> &image,
    float3 expected,
    float tolerance = 1e-4f)
{
  const float3 both[2] = {expected, expected};
  return matches(name, image, both, tolerance);
}

// Per-primitive values of attribute 'name' for the two triangles
SetGeometry primitiveAttribute(const char *name, float4 t0, float4 t1)
{
  const std::string param = std::string("primitive.") + name;
  return [=](anari::Device d, anari::Geometry g) {
    const float4 values[2] = {t0, t1};
    anari::setParameterArray1D(d, g, param.c_str(), values, 2);
  };
}

// Tests //////////////////////////////////////////////////////////////////////

bool testMatte(anari::Device d)
{
  bool pass = true;
  const float3 albedo(0.8f, 0.6f, 0.4f);
  pass &= matches("matte: a Lambertian of its constant colour",
      render(d,
          floorWorld(d, material(d, "matte", [&](anari::Device d, auto m) {
            anari::setParameter(d, m, "color", albedo);
          }))),
      albedo);

  const float4 c0(0.9f, 0.5f, 0.1f, 1.f), c1(0.2f, 0.4f, 0.7f, 1.f);
  const float3 fromColor[2] = {float3(c0.x, c0.y, c0.z),
      float3(c1.x, c1.y, c1.z)};
  pass &= matches("matte: colour from the 'color' attribute",
      render(d,
          floorWorld(d,
              material(d,
                  "matte",
                  [](anari::Device d, auto m) {
                    anari::setParameter(d, m, "color", "color");
                  }),
              true,
              primitiveAttribute("color", c0, c1))),
      fromColor);

  // the floor's normal is +z
  pass &= matches("matte: colour from the ray-derived 'worldNormal'",
      render(d,
          floorWorld(d, material(d, "matte", [](anari::Device d, auto m) {
            anari::setParameter(d, m, "color", "worldNormal");
          }))),
      float3(0.f, 0.f, 1.f));

  // an unknown attribute name is the default colour, the specification's 0.8
  const float3 matteDefault(0.8f);
  pass &= matches("matte: an unknown attribute name is the default colour",
      render(d,
          floorWorld(d, material(d, "matte", [](anari::Device d, auto m) {
            anari::setParameter(d, m, "color", "attribute7");
          }))),
      matteDefault);

  // an unset colour is the specification's 0.8 (helide's is white), natively
  // and through the graph, committed or not, as in the 'preview' renderer
  for (bool native : {true, false}) {
    const std::string how = native ? " (native)" : " (graph)";
    pass &= matches("matte: unset colour is 0.8" + how,
        render(d,
            floorWorld(d, material(d, "matte", [](anari::Device, auto) {})),
            1,
            "quality",
            native),
        matteDefault);
    pass &= matches("matte: never committed is 0.8" + how,
        render(d,
            floorWorld(d, anari::newObject<anari::Material>(d, "matte")),
            1,
            "quality",
            native),
        matteDefault);
  }
  pass &= matches("matte: unset colour is 0.8 in the 'preview' renderer",
      render(d,
          floorWorld(d, material(d, "matte", [](anari::Device, auto) {})),
          1,
          "default"),
      matteDefault);
  return pass;
}

bool testAlpha(anari::Device d)
{
  // mask on 'attribute0': triangle 0 below the cutoff (cut out: the
  // background), triangle 1 above it
  const float3 albedo(0.5f, 0.7f, 0.9f);
  const float3 expected[2] = {
      float3(BACKGROUND.x, BACKGROUND.y, BACKGROUND.z), albedo};
  bool pass = matches("matte: opacity from an attribute, mask",
      render(d,
          floorWorld(d,
              material(d,
                  "matte",
                  [&](anari::Device d, auto m) {
                    anari::setParameter(d, m, "color", albedo);
                    anari::setParameter(d, m, "opacity", "attribute0");
                    anari::setParameter(d, m, "alphaMode", "mask");
                    anari::setParameter(d, m, "alphaCutoff", 0.5f);
                  }),
              true,
              primitiveAttribute("attribute0",
                  float4(0.2f, 0.f, 0.f, 1.f),
                  float4(0.8f, 0.f, 0.f, 1.f)))),
      expected);

  // the same through physicallyBased, the base colour's alpha (an attribute)
  // times the opacity
  pass &= matches("physicallyBased: alpha from the base colour, mask",
      render(d,
          floorWorld(d,
              material(d,
                  "physicallyBased",
                  [&](anari::Device d, auto m) {
                    anari::setParameter(d, m, "baseColor", "color");
                    anari::setParameter(d, m, "opacity", 0.5f);
                    anari::setParameter(d, m, "metallic", 0.f);
                    anari::setParameter(d, m, "alphaMode", "mask");
                    anari::setParameter(d, m, "alphaCutoff", 0.25f);
                  }),
              true,
              primitiveAttribute("color",
                  float4(albedo, 0.4f),
                  float4(albedo, 0.6f)))),
      expected);
  return pass;
}

bool testPhysicallyBased(anari::Device d)
{
  bool pass = true;
  // a dielectric without specular reflection is a Lambertian of its base
  // colour
  const float3 base(0.8f, 0.6f, 0.4f);
  pass &= matches("physicallyBased: a dielectric without specular is "
                  "Lambertian",
      render(d,
          floorWorld(
              d, material(d, "physicallyBased", [&](anari::Device d, auto m) {
                anari::setParameter(d, m, "baseColor", base);
                anari::setParameter(d, m, "metallic", 0.f);
                anari::setParameter(d, m, "specular", 0.f);
              }))),
      base);

  // emission, seen directly (no lights)
  const float3 emissive(0.5f, 0.25f, 0.125f);
  pass &= matches("physicallyBased: constant emission",
      render(d,
          floorWorld(d,
              material(d,
                  "physicallyBased",
                  [&](anari::Device d, auto m) {
                    anari::setParameter(d, m, "emissive", emissive);
                  }),
              false)),
      emissive);

  const float4 e0(1.f, 0.f, 0.5f, 1.f), e1(0.f, 2.f, 0.25f, 1.f);
  const float3 fromColor[2] = {float3(e0.x, e0.y, e0.z),
      float3(e1.x, e1.y, e1.z)};
  pass &= matches("physicallyBased: emission from an attribute",
      render(d,
          floorWorld(d,
              material(d,
                  "physicallyBased",
                  [](anari::Device d, auto m) {
                    anari::setParameter(d, m, "emissive", "attribute3");
                  }),
              false,
              primitiveAttribute("attribute3", e0, e1))),
      fromColor);
  return pass;
}

// A mask cut-out (a C++ material whose presence is 0) over a floor lit by a
// directional light with a cone: camera rays, shadow rays and the
// emitter-only segment after the last bounce (which finds the light by BSDF
// sampling) all pass it, so the image is the floor's without it
bool testCutOut(anari::Device d)
{
  const auto world = [&](bool cutOut) {
    auto w = anari::newObject<anari::World>(d);
    std::vector<anari::Surface> surfaces;
    const auto quad = [&](float z, anari::Material mat) {
      const std::vector<float3> v = {{-1.f, -1.f, z},
          {1.f, -1.f, z},
          {1.f, 1.f, z},
          {-1.f, 1.f, z}};
      const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
      auto geom = anari::newObject<anari::Geometry>(d, "triangle");
      anari::setParameterArray1D(
          d, geom, "vertex.position", v.data(), v.size());
      anari::setParameterArray1D(
          d, geom, "primitive.index", indices.data(), indices.size());
      anari::commitParameters(d, geom);
      auto surface = anari::newObject<anari::Surface>(d);
      anari::setAndReleaseParameter(d, surface, "geometry", geom);
      anari::setAndReleaseParameter(d, surface, "material", mat);
      anari::commitParameters(d, surface);
      surfaces.push_back(surface);
    };
    quad(0.f, material(d, "matte", [](anari::Device d, auto m) {
      anari::setParameter(d, m, "color", float3(0.8f));
    }));
    if (cutOut) {
      quad(1.f, material(d, "physicallyBased", [](anari::Device d, auto m) {
        anari::setParameter(d, m, "opacity", 0.f);
        anari::setParameter(d, m, "alphaMode", "mask");
      }));
    }
    anari::setParameterArray1D(
        d, w, "surface", surfaces.data(), surfaces.size());
    for (auto s : surfaces)
      anari::release(d, s);
    // a wide cone, which BSDF samples find with a large MIS weight, and
    // whose pdf doesn't depend on where the ray leaves from
    auto light = anari::newObject<anari::Light>(d, "directional");
    anari::setParameter(d, light, "direction", float3(0.f, 0.f, -1.f));
    anari::setParameter(d, light, "angularDiameter", 2.f);
    anari::setParameter(d, light, "irradiance", PI);
    anari::commitParameters(d, light);
    anari::setParameterArray1D(d, w, "light", &light, 1);
    anari::release(d, light);
    anari::commitParameters(d, w);
    return w;
  };
  const auto mean = [](const std::vector<float4> &image) {
    double sum = 0.0;
    for (const auto &p : image)
      sum += p.x;
    return image.empty() ? 0.0 : sum / image.size();
  };
  const double without = mean(render(d, world(false), 64));
  const double with = mean(render(d, world(true), 64));
  char detail[64];
  std::snprintf(detail, sizeof(detail), "mean %g vs %g", with, without);
  return report("a C++ mask cut-out is passed by every ray",
      without > 0.05 && std::abs(with - without) <= 0.005 * without,
      detail);
}

// Samplers ///////////////////////////////////////////////////////////////////

// Texture coordinates over the floor: (0, 0) to (1, 1) in 'attribute0', u
// flipped in 'attribute1'
SetGeometry texcoords()
{
  return [](anari::Device d, anari::Geometry g) {
    const float2 uv[4] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
    anari::setParameterArray1D(d, g, "vertex.attribute0", uv, 4);
    const float2 flipped[4] = {{1.f, 0.f}, {0.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}};
    anari::setParameterArray1D(d, g, "vertex.attribute1", flipped, 4);
  };
}

// 'width' x 'height' x 'depth' texels of distinct colours, alpha 1 and 0 in
// a checkerboard
std::vector<float4> texels(int width, int height = 1, int depth = 1)
{
  std::vector<float4> t;
  for (int k = 0; k < depth; k++) {
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
        t.push_back(float4(float(i + 1) / width,
            float(j + 1) / height,
            float(k + 1) / depth,
            (i + j + k) % 2 ? 1.f : 0.f));
      }
    }
  }
  return t;
}

using SetSampler = std::function<void(anari::Device, anari::Sampler)>;

anari::Sampler sampler(
    anari::Device d, const char *subtype, const SetSampler &set)
{
  auto s = anari::newObject<anari::Sampler>(d, subtype);
  set(d, s);
  anari::commitParameters(d, s);
  return s;
}

// A 4 x 4 image2D with 'filter' and 'wrap' in both directions, more
// parameters from 'set'
anari::Sampler image2D(anari::Device d,
    const char *filter,
    const char *wrap,
    const SetSampler &set = {})
{
  return sampler(d, "image2D", [&](anari::Device d, anari::Sampler s) {
    const auto t = texels(4, 4);
    anari::setParameterArray2D(d, s, "image", t.data(), 4, 4);
    anari::setParameter(d, s, "filter", filter);
    anari::setParameter(d, s, "wrapMode1", wrap);
    anari::setParameter(d, s, "wrapMode2", wrap);
    if (set)
      set(d, s);
  });
}

// A matte floor with texture coordinates whose parameter 'name' is
// 'sampler' (released)
anari::World samplerFloor(anari::Device d,
    anari::Sampler s,
    const char *name = "color",
    const SetMaterial &more = {})
{
  auto mat = material(d, "matte", [&](anari::Device d, auto m) {
    anari::setParameter(d, m, name, s);
    if (more)
      more(d, m);
  });
  anari::release(d, s);
  return floorWorld(d, mat, true, texcoords());
}

// Whether the pathtracer's image of 'world' (released) matches the preview
// renderer's: the pathtracer jitters within the pixel and the preview
// renderer samples its center, so pixels whose 3 x 3 neighbourhood the
// preview renderer shows in one colour must match exactly (at least
// 'minUniform' of them), and the others within 'tolerance' (where filtering
// is linear, the mean over the pixel is close to the value at its center)
bool matchesDefault(const std::string &name,
    anari::Device d,
    anari::World world,
    int minUniform,
    float tolerance = INFINITY)
{
  anari::retain(d, world);
  const std::vector<float4> expected = render(d, world, 1, "default");
  const std::vector<float4> image = render(d, world, 16);
  if (image.empty() || expected.size() != image.size())
    return report(name, false, "no image");

  const int w = int(IMAGE_SIZE.x), h = int(IMAGE_SIZE.y);
  float uniformError = 0.f, otherError = 0.f;
  int uniform = 0, other = 0;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const float4 e = expected[size_t(y) * w + x];
      bool same = true;
      for (int j = std::max(y - 1, 0); j <= std::min(y + 1, h - 1); j++) {
        for (int i = std::max(x - 1, 0); i <= std::min(x + 1, w - 1); i++) {
          const float4 n = expected[size_t(j) * w + i];
          same = same && n.x == e.x && n.y == e.y && n.z == e.z;
        }
      }
      const float4 p = image[size_t(y) * w + x];
      float error = 0.f;
      for (int c = 0; c < 3; c++)
        error = std::max(error, std::abs(p[c] - e[c]));
      if (same) {
        uniformError = std::max(uniformError, error);
        uniform++;
      } else if (std::isfinite(tolerance)) {
        otherError = std::max(otherError, error);
        other++;
      }
    }
  }
  char detail[160];
  std::snprintf(detail,
      sizeof(detail),
      "%d uniform pixels, largest error %g; %d others, largest error %g",
      uniform,
      uniformError,
      other,
      otherError);
  return report(name,
      uniform >= minUniform && uniformError <= 1e-4f && otherError <= tolerance,
      detail);
}

// Tolerance of linearly filtered pixels: the mean over the pixel's 16
// samples against the value at its center
constexpr float LINEAR_TOLERANCE = 0.02f;

mat4 columns(float4 c0, float4 c1, float4 c2, float4 c3)
{
  return mat4(c0, c1, c2, c3);
}

const mat4 SCALE2 = columns(float4(2.f, 0.f, 0.f, 0.f),
    float4(0.f, 2.f, 0.f, 0.f),
    float4(0.f, 0.f, 1.f, 0.f),
    float4(0.f, 0.f, 0.f, 1.f));

bool testSamplers(anari::Device d)
{
  bool pass = true;
  pass &= matchesDefault("image2D nearest clampToEdge, in-transform and offset",
      d,
      samplerFloor(d,
          image2D(d,
              "nearest",
              "clampToEdge",
              [](anari::Device d, anari::Sampler s) {
                anari::setParameter(d, s, "inTransform", SCALE2);
                anari::setParameter(
                    d, s, "inOffset", float4(-0.5f, -0.5f, 0.f, 0.f));
              })),
      500);
  pass &= matchesDefault("image2D nearest repeat",
      d,
      samplerFloor(d,
          image2D(d,
              "nearest",
              "repeat",
              [](anari::Device d, anari::Sampler s) {
                anari::setParameter(d, s, "inTransform", SCALE2);
              })),
      300);
  pass &= matchesDefault("image2D nearest mirrorRepeat",
      d,
      samplerFloor(d,
          image2D(d,
              "nearest",
              "mirrorRepeat",
              [](anari::Device d, anari::Sampler s) {
                anari::setParameter(d, s, "inTransform", SCALE2);
                anari::setParameter(
                    d, s, "inOffset", float4(-1.f, -1.f, 0.f, 0.f));
              })),
      300);
  // 30 degrees about z
  const float c = std::cos(PI / 6.f), sn = std::sin(PI / 6.f);
  pass &= matchesDefault("image2D linear repeat, rotated input",
      d,
      samplerFloor(d,
          image2D(d,
              "linear",
              "repeat",
              [&](anari::Device d, anari::Sampler s) {
                anari::setParameter(d,
                    s,
                    "inTransform",
                    columns(float4(c, sn, 0.f, 0.f),
                        float4(-sn, c, 0.f, 0.f),
                        float4(0.f, 0.f, 1.f, 0.f),
                        float4(0.f, 0.f, 0.f, 1.f)));
              })),
      0,
      LINEAR_TOLERANCE);
  pass &= matchesDefault("image2D nearest, out-transform and offset",
      d,
      samplerFloor(d,
          image2D(d,
              "nearest",
              "clampToEdge",
              [](anari::Device d, anari::Sampler s) {
                // a grey, then offset (at most 1: the pathtracer's
                // materials clamp their colours)
                anari::setParameter(d,
                    s,
                    "outTransform",
                    columns(float4(0.25f, 0.25f, 0.25f, 0.f),
                        float4(0.5f, 0.5f, 0.5f, 0.f),
                        float4(0.1f, 0.1f, 0.1f, 0.f),
                        float4(0.f, 0.f, 0.f, 1.f)));
                anari::setParameter(
                    d, s, "outOffset", float4(0.1f, 0.f, 0.05f, 0.f));
              })),
      500);
  pass &= matchesDefault("image2D from attribute1",
      d,
      samplerFloor(d,
          image2D(d,
              "nearest",
              "clampToEdge",
              [](anari::Device d, anari::Sampler s) {
                anari::setParameter(d, s, "inAttribute", "attribute1");
              })),
      500);
  pass &= matchesDefault("image2D from the ray-derived worldPosition",
      d,
      samplerFloor(d,
          image2D(d,
              "nearest",
              "clampToEdge",
              [](anari::Device d, anari::Sampler s) {
                // [-1, 1] to [0, 1]
                anari::setParameter(d, s, "inAttribute", "worldPosition");
                anari::setParameter(d,
                    s,
                    "inTransform",
                    columns(float4(0.5f, 0.f, 0.f, 0.f),
                        float4(0.f, 0.5f, 0.f, 0.f),
                        float4(0.f, 0.f, 1.f, 0.f),
                        float4(0.f, 0.f, 0.f, 1.f)));
                anari::setParameter(
                    d, s, "inOffset", float4(0.5f, 0.5f, 0.f, 0.f));
              })),
      500);
  pass &= matchesDefault("image1D linear, input mixing u and v",
      d,
      samplerFloor(d,
          sampler(d,
              "image1D",
              [](anari::Device d, anari::Sampler s) {
                const auto t = texels(5);
                anari::setParameterArray1D(d, s, "image", t.data(), 5);
                anari::setParameter(d,
                    s,
                    "inTransform",
                    columns(float4(0.7f, 0.f, 0.f, 0.f),
                        float4(0.3f, 0.f, 0.f, 0.f),
                        float4(0.f, 0.f, 1.f, 0.f),
                        float4(0.f, 0.f, 0.f, 1.f)));
              })),
      0,
      LINEAR_TOLERANCE);
  pass &= matchesDefault("image3D linear, depth from the input's w",
      d,
      samplerFloor(d,
          sampler(d,
              "image3D",
              [](anari::Device d, anari::Sampler s) {
                const auto t = texels(3, 3, 3);
                anari::setParameterArray3D(d, s, "image", t.data(), 3, 3, 3);
                // z = 0.6 w (attribute0's w is 1)
                anari::setParameter(d,
                    s,
                    "inTransform",
                    columns(float4(1.f, 0.f, 0.f, 0.f),
                        float4(0.f, 1.f, 0.f, 0.f),
                        float4(0.f, 0.f, 1.f, 0.f),
                        float4(0.f, 0.f, 0.6f, 1.f)));
              })),
      0,
      LINEAR_TOLERANCE);
  pass &= matchesDefault("transform: the complement of the input",
      d,
      samplerFloor(d,
          sampler(d,
              "transform",
              [](anari::Device d, anari::Sampler s) {
                anari::setParameter(d,
                    s,
                    "outTransform",
                    columns(float4(-1.f, 0.f, 0.f, 0.f),
                        float4(0.f, -1.f, 0.f, 0.f),
                        float4(0.f, 0.f, -1.f, 0.f),
                        float4(0.f, 0.f, 0.f, 1.f)));
                anari::setParameter(
                    d, s, "outOffset", float4(1.f, 1.f, 0.5f, 0.f));
              })),
      0,
      LINEAR_TOLERANCE);
  pass &= matchesDefault("primitive: per primitive, with an offset",
      d,
      samplerFloor(d,
          sampler(d,
              "primitive",
              [](anari::Device d, anari::Sampler s) {
                const float4 values[3] = {float4(1.f, 0.f, 0.f, 1.f),
                    float4(0.2f, 0.7f, 0.1f, 1.f),
                    float4(0.3f, 0.4f, 0.9f, 1.f)};
                anari::setParameterArray1D(d, s, "array", values, 3);
                anari::setParameter(d, s, "inOffset", uint64_t(1));
              })),
      800);
  pass &= matchesDefault("inAttribute none: (0, 0, 0, 1)",
      d,
      samplerFloor(d,
          image2D(d,
              "nearest",
              "clampToEdge",
              [](anari::Device d, anari::Sampler s) {
                anari::setParameter(d, s, "inAttribute", "none");
              })),
      1000);
  pass &= matchesDefault("an invalid sampler is the parameter's default",
      d,
      samplerFloor(
          d, sampler(d, "image2D", [](anari::Device, anari::Sampler) {})),
      1000);
  pass &= matchesDefault("physicallyBased: base colour from a sampler",
      d,
      floorWorld(d,
          material(d,
              "physicallyBased",
              [&](anari::Device d, auto m) {
                auto s = image2D(d, "nearest", "clampToEdge");
                anari::setParameter(d, m, "baseColor", s);
                anari::setParameter(d, m, "metallic", 0.f);
                anari::release(d, s);
              }),
          true,
          texcoords()),
      500);
  // the texels' alpha is 0 or 1: a mask cuts out half the texels
  pass &= matchesDefault("matte: opacity from the colour sampler's alpha, mask",
      d,
      samplerFloor(d,
          image2D(d, "nearest", "clampToEdge"),
          "color",
          [](anari::Device d, auto m) {
            anari::setParameter(d, m, "alphaMode", "mask");
          }),
      500);
  pass &= matchesDefault("matte: opacity from its own sampler, blend",
      d,
      samplerFloor(d,
          sampler(d,
              "transform",
              [](anari::Device d, anari::Sampler s) {
                anari::setParameter(d, s, "inAttribute", "attribute0");
              }),
          "opacity",
          [](anari::Device d, auto m) {
            anari::setParameter(d, m, "color", float3(0.8f, 0.6f, 0.4f));
            anari::setParameter(d, m, "alphaMode", "blend");
          }),
      0,
      // blend is stochastic presence in the pathtracer
      0.15f);
  return pass;
}

// A sampler committed again (its inAttribute and image changed) changes what
// the pathtracer samples: the material builds its graph again
bool testSamplerChanges(anari::Device d)
{
  auto s = image2D(d, "nearest", "clampToEdge");
  anari::retain(d, s);
  auto world = samplerFloor(d, s);
  anari::retain(d, world);
  bool pass = matchesDefault("a sampler before it changes", d, world, 500);

  anari::setParameter(d, s, "inAttribute", "attribute1");
  const auto t = texels(2, 2);
  anari::setParameterArray2D(d, s, "image", t.data(), 2, 2);
  anari::commitParameters(d, s);
  pass &=
      matchesDefault("the sampler's new inAttribute and image", d, world, 500);
  anari::release(d, s);
  return pass;
}

// The samplers-only parameters of physicallyBased: 'normal' tilts the
// normal, 'occlusion' isn't used (and says so)
bool testSamplerOnlyParameters(anari::Device d)
{
  const float3 base(0.8f, 0.6f, 0.4f);
  const auto floor = [&](const char *name, anari::Sampler s) {
    return floorWorld(
        d, material(d, "physicallyBased", [&](anari::Device d, auto m) {
          anari::setParameter(d, m, "baseColor", base);
          anari::setParameter(d, m, "metallic", 0.f);
          if (s) {
            anari::setParameter(d, m, name, s);
            anari::release(d, s);
          }
        }));
  };
  const auto constant = [&](float4 v) {
    return sampler(d, "image1D", [&](anari::Device d, anari::Sampler s) {
      anari::setParameterArray1D(d, s, "image", &v, 1);
    });
  };

  // a tangent-space normal tilted to cos 0.8 from the floor's, under a light
  // straight down
  bool pass = matches("physicallyBased: a normal map tilts the normal",
      render(d, floor("normal", constant(float4(0.6f, 0.f, 0.8f, 1.f)))),
      base * 0.8f);
  pass &= matches("physicallyBased: an invalid normal map is the normal",
      render(d,
          floor("normal",
              sampler(d, "image2D", [](anari::Device, anari::Sampler) {}))),
      base);

  g_debug.clear();
  pass &= matches("physicallyBased: 'occlusion' isn't used",
      render(d, floor("occlusion", constant(float4(0.25f)))),
      base);
  bool told = false;
  for (const auto &m : g_debug)
    told = told || m.find("doesn't use 'occlusion'") != std::string::npos;
  pass &= report("'occlusion' is reported as not used (DEBUG)", told);
  return pass;
}

// openPBR and usdPreviewSurface, set to a Lambertian of their colour (no
// specular reflection under a light straight down): constants, attributes,
// emission, a mask cut-out and a normal map
bool testMaterialModels(anari::Device d)
{
  const float3 base(0.8f, 0.6f, 0.4f);
  const auto openPbr = [&](const SetMaterial &more) {
    return material(d, "openPBR", [&](anari::Device d, anari::Material m) {
      anari::setParameter(d, m, "base_color", base);
      anari::setParameter(d, m, "specular_weight", 0.f);
      if (more)
        more(d, m);
    });
  };
  const auto usd = [&](const SetMaterial &more) {
    return material(
        d, "usdPreviewSurface", [&](anari::Device d, anari::Material m) {
          anari::setParameter(d, m, "diffuseColor", base);
          // F0 0: no reflection at normal incidence
          anari::setParameter(d, m, "useSpecularWorkflow", 1);
          if (more)
            more(d, m);
        });
  };
  const auto constant = [&](float4 v) {
    return sampler(d, "image1D", [&](anari::Device d, anari::Sampler s) {
      anari::setParameterArray1D(d, s, "image", &v, 1);
    });
  };
  const auto normalMap = [&](const char *name) {
    return [=](anari::Device d, anari::Material m) {
      auto s = constant(float4(0.6f, 0.f, 0.8f, 1.f));
      anari::setParameter(d, m, name, s);
      anari::release(d, s);
    };
  };

  bool pass = matches("openPBR: a Lambertian of its base colour",
      render(d, floorWorld(d, openPbr({}))),
      base);
  pass &= matches("usdPreviewSurface: a Lambertian of its diffuse colour",
      render(d, floorWorld(d, usd({}))),
      base);

  const float4 c0(0.9f, 0.5f, 0.1f, 1.f), c1(0.2f, 0.4f, 0.7f, 1.f);
  const float3 fromColor[2] = {float3(c0.x, c0.y, c0.z),
      float3(c1.x, c1.y, c1.z)};
  pass &= matches("openPBR: base_color from an attribute",
      render(d,
          floorWorld(d,
              openPbr([](anari::Device d, anari::Material m) {
                anari::setParameter(d, m, "base_color", "attribute1");
              }),
              true,
              primitiveAttribute("attribute1", c0, c1))),
      fromColor);

  const float3 emissive(0.5f, 0.25f, 0.125f);
  pass &= matches("openPBR: emission",
      render(d,
          floorWorld(d,
              openPbr([&](anari::Device d, anari::Material m) {
                anari::setParameter(d, m, "emission_color", emissive);
                anari::setParameter(d, m, "emission_luminance", 1.f);
              }),
              false)),
      emissive);
  pass &= matches("usdPreviewSurface: emission",
      render(d,
          floorWorld(d,
              usd([&](anari::Device d, anari::Material m) {
                anari::setParameter(d, m, "emissiveColor", emissive);
              }),
              false)),
      emissive);

  const float3 cut[2] = {float3(BACKGROUND.x, BACKGROUND.y, BACKGROUND.z),
      base};
  pass &= matches("usdPreviewSurface: opacityThreshold cuts out",
      render(d,
          floorWorld(d,
              usd([](anari::Device d, anari::Material m) {
                anari::setParameter(d, m, "opacity", "attribute0");
                anari::setParameter(d, m, "opacityThreshold", 0.5f);
              }),
              true,
              primitiveAttribute("attribute0",
                  float4(0.2f, 0.f, 0.f, 1.f),
                  float4(0.8f, 0.f, 0.f, 1.f)))),
      cut);

  // openPBR: a world-space geometry_normal tilted to cos 0.8 (0.6 on
  // triangle 1) from the floor's, as a constant, an attribute or a sampler's
  // value as given
  const float3 tilted(0.6f, 0.f, 0.8f);
  pass &= matches("openPBR: a constant world-space geometry_normal",
      render(d, floorWorld(d, openPbr([&](anari::Device d, anari::Material m) {
        anari::setParameter(d, m, "geometry_normal", tilted);
      }))),
      base * 0.8f);
  const float3 byTriangle[2] = {base * 0.8f, base * 0.6f};
  pass &= matches("openPBR: geometry_normal from an attribute",
      render(d,
          floorWorld(d,
              openPbr([](anari::Device d, anari::Material m) {
                anari::setParameter(d, m, "geometry_normal", "attribute2");
              }),
              true,
              primitiveAttribute("attribute2",
                  float4(0.6f, 0.f, 0.8f, 1.f),
                  float4(0.f, -0.8f, 0.6f, 1.f)))),
      byTriangle);
  pass &= matches("openPBR: geometry_normal from a sampler, as given",
      render(d, floorWorld(d, openPbr([&](anari::Device d, anari::Material m) {
        auto s = constant(float4(tilted.x, tilted.y, tilted.z, 1.f));
        anari::setParameter(d, m, "geometry_normal", s);
        anari::release(d, s);
      }))),
      base * 0.8f);

  // an invalid sampler or a name that isn't an attribute counts as unset:
  // the nodedef's Nworld, here the floor's shading normal tilted to cos 0.8,
  // not a constant +z
  const auto tiltedNormals = [&](anari::Device d, anari::Geometry g) {
    const std::vector<float3> n(4, tilted);
    anari::setParameterArray1D(d, g, "vertex.normal", n.data(), n.size());
  };
  const auto tiltedFloor = [&](const char *normal, anari::Sampler s) {
    return floorWorld(d,
        openPbr([&](anari::Device d, anari::Material m) {
          if (s)
            anari::setParameter(d, m, "geometry_normal", s);
          else if (normal)
            anari::setParameter(d, m, "geometry_normal", normal);
        }),
        true,
        tiltedNormals);
  };
  pass &= matches("openPBR: unset geometry_normal is the shading normal",
      render(d, tiltedFloor(nullptr, nullptr)),
      base * 0.8f);
  auto invalid = sampler(d, "image2D", [](anari::Device, anari::Sampler) {});
  pass &= matches("openPBR: an invalid sampler on geometry_normal is unset",
      render(d, tiltedFloor(nullptr, invalid)),
      base * 0.8f);
  anari::release(d, invalid);
  pass &= matches("openPBR: geometry_normal naming no attribute is unset",
      render(d, tiltedFloor("notAnAttribute", nullptr)),
      base * 0.8f);

  // a tangent-space normal tilted to cos 0.8 from the floor's
  pass &= matches("usdPreviewSurface: a tangent-space normal tilts it",
      render(d, floorWorld(d, usd(normalMap("normal")))),
      base * 0.8f);

  g_debug.clear();
  pass &= matches("usdPreviewSurface: 'occlusion' and 'displacement' aren't "
                  "used",
      render(d,
          floorWorld(d,
              usd([](anari::Device d, anari::Material m) {
                anari::setParameter(d, m, "occlusion", 0.25f);
                anari::setParameter(d, m, "displacement", 1.f);
              }))),
      base);
  int told = 0;
  for (const auto &m : g_debug) {
    told += m.find("doesn't use 'occlusion'") != std::string::npos;
    told += m.find("doesn't use 'displacement'") != std::string::npos;
  }
  pass &= report("usdPreviewSurface reports 'occlusion' and 'displacement' "
                 "as not used (DEBUG)",
      told >= 2);
  return pass;
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

  bool pass = testMatte(d);
  pass &= testAlpha(d);
  pass &= testPhysicallyBased(d);
  pass &= testCutOut(d);
  pass &= testSamplers(d);
  pass &= testSamplerChanges(d);
  pass &= testSamplerOnlyParameters(d);
  pass &= testMaterialModels(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no warnings or errors", g_errors == 0);
  return pass ? 0 : 1;
}
