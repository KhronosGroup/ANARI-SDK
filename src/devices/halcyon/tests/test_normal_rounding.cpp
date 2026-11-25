// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The shading normal's rounding under near-delta GGX lobes. Typhoon's GGX lobes
// take the smooth normal the shading context hands them as unit length, and
// just above the delta threshold (alpha 1e-3) their albedo depends on that
// length's last bits: at roughness 0.04 (alpha 0.0016) a length of 1 + 1.2e-7
// (one ulp) reflects about 9% more, 1 - 6e-8 (one ulp) 4.4% less. The
// pathtracer normalizes the world-space smooth normal as Typhoon does, in
// double with each component rounded to the nearest float, whose length errs to
// both sides of 1, so that over a curved surface the errors cancel; a float
// normalize() erred to one side on average (which side depends on the mesh and
// its transform).
//
// The scene: a tessellated unit sphere with smooth vertex normals, turned
// and scaled by its instance (so that its normals go through the normal
// transform), of white standard_surface metal under the ambient light of
// radiance 1, seen from +z. The checks:
// - the property itself: over the sphere's pixels, the shading context's
//   normal (the 'normal' debug mode, FLOAT32) has a mean length within 2e-9
//   of 1 (correctly rounded components: 3e-11 here; a float
//   normalize(): -1.9e-8);
// - its effect: the central disc (where the view is within 53 degrees of the
//   normal) renders at roughness 0.04 as at roughness 0.08 (alpha 0.0064, 16
//   times less sensitive), within 0.4%. With a float normalize() the
//   roughness-0.04 sphere rendered 1.4% darker. The two agree over the whole
//   disc to about 0.05% (the spread over seeds), partly as regions cancel:
//   over the central 27 degrees alone roughness 0.04 is 0.2% darker, the
//   lobe's own rounding, which Typhoon's evaluation shares.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

int g_errors = 0;
int g_warnings = 0;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR) {
    std::fprintf(stderr, "[ANARI ERROR] %s\n", message);
    g_errors++;
  } else if (severity == ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI WARNING] %s\n", message);
    g_warnings++;
  }
}

bool report(const std::string &name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

// White standard_surface metal of 'roughness'
anari::Material makeMetal(anari::Device d, float roughness)
{
  char document[1024];
  std::snprintf(document,
      sizeof(document),
      R"(<?xml version="1.0"?>
<materialx version="1.39">
  <standard_surface name="shader" type="surfaceshader">
    <input name="base" type="float" value="1" />
    <input name="base_color" type="color3" value="1, 1, 1" />
    <input name="specular" type="float" value="1" />
    <input name="specular_color" type="color3" value="1, 1, 1" />
    <input name="specular_roughness" type="float" value="%g" />
    <input name="metalness" type="float" value="1" />
  </standard_surface>
  <surfacematerial name="Metal" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="shader" />
  </surfacematerial>
</materialx>
)",
      roughness);
  auto mat = anari::newObject<anari::Material>(d, "materialx");
  anari::setParameter(d, mat, "sourceType", "documentInline");
  anari::setParameter(d, mat, "source", document);
  anari::commitParameters(d, mat);
  return mat;
}

// A unit sphere of triangles, 'rings' of latitude by 2 'rings' of longitude,
// its vertex normals the unit positions
anari::Geometry makeSphereMesh(anari::Device d, int rings)
{
  const float pi = 3.14159265358979f;
  const int segments = 2 * rings;
  std::vector<float3> positions;
  for (int i = 0; i <= rings; i++) {
    const float theta = pi * i / rings;
    for (int j = 0; j <= segments; j++) {
      const float phi = 2.f * pi * j / segments;
      positions.push_back(float3(std::sin(theta) * std::cos(phi),
          std::sin(theta) * std::sin(phi),
          std::cos(theta)));
    }
  }
  std::vector<uint3> indices;
  for (int i = 0; i < rings; i++) {
    for (int j = 0; j < segments; j++) {
      const uint32_t a = i * (segments + 1) + j;
      const uint32_t b = a + segments + 1;
      if (i > 0)
        indices.push_back(uint3(a, b, a + 1));
      if (i < rings - 1)
        indices.push_back(uint3(a + 1, b, b + 1));
    }
  }
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", positions.data(), positions.size());
  anari::setParameterArray1D(
      d, geom, "vertex.normal", positions.data(), positions.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  return geom;
}

// The sphere of 'mat' (released), turned about a skew axis and scaled by
// 1.7 by its instance
anari::World makeWorld(anari::Device d, anari::Material mat)
{
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", makeSphereMesh(d, 96));
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "surface", &surface, 1);
  anari::commitParameters(d, group);
  anari::release(d, surface);

  const float3 axis = normalize(float3(0.3f, 0.8f, 0.5f));
  const mat4 xfm = mul(rotation_matrix(rotation_quat(axis, 0.7f)),
      scaling_matrix(float3(1.7f)));
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::setParameter(d, inst, "transform", xfm);
  anari::commitParameters(d, inst);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &inst, 1);
  anari::commitParameters(d, world);
  anari::release(d, inst);
  return world;
}

constexpr uint32_t SIZE = 64;
constexpr float VIEW_HEIGHT = 1.7f * 2.2f;

// The sphere seen from +z by an orthographic camera, in the pathtracer's
// 'mode'
std::vector<float4> render(anari::Device d, anari::World world, const char *mode)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  anari::setParameter(d, renderer, "mode", mode);
  anari::setParameter(d, renderer, "background", float4(1.f));
  anari::setParameter(d, renderer, "ambientColor", float3(1.f));
  anari::setParameter(d, renderer, "ambientRadiance", 1.f);
  anari::setParameter(d, renderer, "pixelSamples", 16);
  anari::setParameter(d, renderer, "minSamplesBeforeAdaptive", -1);
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 6.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", VIEW_HEIGHT);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", uint2(SIZE));
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);
  std::vector<float4> image;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && fb.width == SIZE && fb.height == SIZE)
    image.assign(fb.data, fb.data + SIZE * SIZE);
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);
  return image;
}

// Whether pixel (x, y) is within 'radius' of the sphere's center (in units of
// its radius)
bool within(uint32_t x, uint32_t y, float radius)
{
  const float px = ((x + 0.5f) / SIZE - 0.5f) * VIEW_HEIGHT / 1.7f;
  const float py = ((y + 0.5f) / SIZE - 0.5f) * VIEW_HEIGHT / 1.7f;
  return px * px + py * py < radius * radius;
}

// The mean of the sphere's central disc (radius 0.8 of the sphere's) over
// its pixels and channels
double discMean(anari::Device d, anari::World world)
{
  const auto image = render(d, world, "default");
  double sum = 0.;
  size_t count = 0;
  for (uint32_t y = 0; y < SIZE && !image.empty(); y++) {
    for (uint32_t x = 0; x < SIZE; x++) {
      if (!within(x, y, 0.8f))
        continue;
      const float4 &p = image[size_t(y) * SIZE + x];
      sum += double(p.x) + p.y + p.z;
      count += 3;
    }
  }
  return count ? sum / count : 0.;
}

// The mean of the shading context's normal's length minus 1 over the
// sphere's pixels (within 0.95 of its radius), and their count
double normalLengthError(anari::Device d, anari::World world, size_t &count)
{
  const auto image = render(d, world, "normal");
  double sum = 0.;
  count = 0;
  for (uint32_t y = 0; y < SIZE && !image.empty(); y++) {
    for (uint32_t x = 0; x < SIZE; x++) {
      if (!within(x, y, 0.95f))
        continue;
      const float4 &n = image[size_t(y) * SIZE + x];
      sum += std::sqrt(double(n.x) * n.x + double(n.y) * n.y
                 + double(n.z) * n.z)
          - 1.;
      count++;
    }
  }
  return count ? sum / count : 1.;
}

} // namespace

int main()
{
  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "could not load the halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");
  if (!d) {
    std::fprintf(stderr, "could not create the halcyon device\n");
    return 1;
  }

  auto sharp = makeWorld(d, makeMetal(d, 0.04f));
  size_t normals = 0;
  const double lengthError = normalLengthError(d, sharp, normals);
  std::printf("  mean length of %zu normals - 1: %.3e\n", normals, lengthError);
  bool ok = report("the shading normal's mean length is 1 (within 2e-9)",
      normals > 1000 && std::abs(lengthError) <= 2e-9);

  auto rough = makeWorld(d, makeMetal(d, 0.08f));
  const double meanSharp = discMean(d, sharp);
  const double meanRough = discMean(d, rough);
  anari::release(d, sharp);
  anari::release(d, rough);

  const double ratio = meanRough > 0. ? meanSharp / meanRough : 0.;
  std::printf("  roughness 0.04: %.6f, roughness 0.08: %.6f (ratio %.5f)\n",
      meanSharp,
      meanRough,
      ratio);
  ok &= report("a roughness-0.04 metal sphere renders as a roughness-0.08 "
               "one (within 0.4%)",
      std::abs(ratio - 1.) <= 4e-3);

  anari::release(d, d);
  anari::unloadLibrary(library);

  ok &= report("no errors or warnings", g_errors == 0 && g_warnings == 0);
  return ok ? 0 : 1;
}
