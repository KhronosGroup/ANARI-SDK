// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Face-varying attributes ('faceVarying.*', one value per primitive corner)
// on triangle and quad geometry, through the device:
// - an indexed triangle mesh and an indexed quad mesh whose corners at a
//   shared vertex have different face-varying values render each primitive
//   with its own corner values, interpolated like vertex values, in the
//   'geometry.color' and 'geometry.attribute0' modes
// - 'faceVarying.color' overrides 'vertex.color'; once unset again, the
//   geometry shows 'vertex.color'
// - a face-varying array shorter than the corner count warns once, and the
//   corners past its end use its last element

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};
const float3 VERTEX_COLOR = {0.5f, 0.5f, 0.5f};

// Triangles in x = [-0.9, -0.1], y = [-0.4, 0.4], sharing edge 0-2
//   3---2
//   |t1/|
//   | /t0
//   0---1
const std::vector<float3> TRI_VERTICES = {{-0.9f, -0.4f, 0.f},
    {-0.1f, -0.4f, 0.f},
    {-0.1f, 0.4f, 0.f},
    {-0.9f, 0.4f, 0.f}};
const std::vector<uint3> TRI_INDICES = {{0, 1, 2}, {0, 2, 3}};

// Quads in x = [0.1, 0.9], y = [-0.4, 0.4], sharing edge 1-4; q1 starts at
// its lower-right corner, as in test_quad.cpp
//   3---4---5
//   |q0 |q1 |
//   0---1---2
const std::vector<float3> QUAD_VERTICES = {{0.1f, -0.4f, 0.f},
    {0.5f, -0.4f, 0.f},
    {0.9f, -0.4f, 0.f},
    {0.1f, 0.4f, 0.f},
    {0.5f, 0.4f, 0.f},
    {0.9f, 0.4f, 0.f}};
const std::vector<uint4> QUAD_INDICES = {{0, 1, 4, 3}, {2, 5, 4, 1}};

// One value per corner: 3 per triangle, 4 per quad
const std::vector<float3> TRI_COLORS = {{1.f, 0.f, 0.f},
    {0.f, 1.f, 0.f},
    {0.f, 0.f, 1.f},
    {1.f, 1.f, 0.f},
    {0.f, 1.f, 1.f},
    {1.f, 0.f, 1.f}};
// attribute0 values stay in [0, 1], as the colour channel clamps
const std::vector<float2> TRI_ATTR0 = {{0.f, 0.1f},
    {0.2f, 0.3f},
    {0.4f, 0.5f},
    {0.6f, 0.7f},
    {0.8f, 0.9f},
    {1.f, 0.f}};
const std::vector<float3> QUAD_COLORS = {{1.f, 0.f, 0.f},
    {0.f, 1.f, 0.f},
    {0.f, 0.f, 1.f},
    {1.f, 1.f, 1.f},
    {0.5f, 0.f, 0.f},
    {0.f, 0.5f, 0.f},
    {0.f, 0.f, 0.5f},
    {0.5f, 0.5f, 0.5f}};
const std::vector<float2> QUAD_ATTR0 = {{0.f, 0.9f},
    {0.1f, 0.8f},
    {0.2f, 0.7f},
    {0.3f, 0.6f},
    {0.4f, 0.5f},
    {0.5f, 0.4f},
    {0.6f, 0.3f},
    {0.7f, 0.2f}};

// Status messages at WARNING severity or worse
std::vector<std::string> g_errors;
std::vector<std::string> g_warnings;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR) {
    std::fprintf(stderr, "[ANARI] ERROR: %s\n", message);
    g_errors.push_back(message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    g_warnings.push_back(message);
  }
}

bool check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

// Pixel covering 'pos', and the position its ray (through the pixel centre)
// hits the z = 0 plane at
uint2 pixelAt(float2 pos)
{
  return uint2(uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x),
      uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y));
}

float2 pixelCentre(uint2 px)
{
  return float2(2.f * (px.x + 0.5f) / IMAGE_SIZE.x - 1.f,
      2.f * (px.y + 0.5f) / IMAGE_SIZE.y - 1.f);
}

float2 xy(float3 v)
{
  return float2(v.x, v.y);
}

// Face-varying value of triangle 't' at 'p': barycentric in the triangle's
// corners 3t, 3t + 1, 3t + 2 (Embree's (1 - u - v, u, v)); 'values' shorter
// than the corner count clamp to their last element
template <typename T>
T triangleValue(const std::vector<T> &values, uint32_t t, float2 p)
{
  const uint3 idx = TRI_INDICES[t];
  const float2 a = xy(TRI_VERTICES[idx.x]);
  const float2 e1 = xy(TRI_VERTICES[idx.y]) - a;
  const float2 e2 = xy(TRI_VERTICES[idx.z]) - a;
  const float2 d = p - a;
  const float det = e1.x * e2.y - e1.y * e2.x;
  const float u = (d.x * e2.y - d.y * e2.x) / det;
  const float v = (e1.x * d.y - e1.y * d.x) / det;
  auto corner = [&](uint32_t i) {
    return values[std::min<size_t>(3 * t + i, values.size() - 1)];
  };
  return (1.f - u - v) * corner(0) + u * corner(1) + v * corner(2);
}

// Face-varying value of quad 'q' at 'p': bilinear in the quad's corners
// 4q..4q + 3 at (u, v) = (0, 0), (1, 0), (1, 1), (0, 1)
template <typename T>
T quadValue(const std::vector<T> &values, uint32_t q, float2 p)
{
  const uint4 idx = QUAD_INDICES[q];
  const float2 a = xy(QUAD_VERTICES[idx.x]);
  const float2 eu = xy(QUAD_VERTICES[idx.y]) - a;
  const float2 ev = xy(QUAD_VERTICES[idx.w]) - a;
  // the quads are rectangles, so (u, v) are projections onto the edges
  const float u = linalg::dot(p - a, eu) / linalg::dot(eu, eu);
  const float v = linalg::dot(p - a, ev) / linalg::dot(ev, ev);
  const T *c = &values[4 * q];
  return (1.f - v) * (1.f - u) * c[0] + (1.f - v) * u * c[1] + v * u * c[2]
      + v * (1.f - u) * c[3];
}

enum class Mesh
{
  TRIANGLES,
  QUADS
};

struct Probe
{
  const char *name;
  Mesh mesh;
  uint32_t prim;
  float2 pos;
};

// Two probes per primitive, the ones of a mesh's primitives near their shared
// edge, where the corners share vertices but not values
const std::vector<Probe> PROBES = {
    {"t0", Mesh::TRIANGLES, 0, {-0.25f, -0.2f}},
    {"t0 near shared edge", Mesh::TRIANGLES, 0, {-0.4f, -0.05f}},
    {"t1", Mesh::TRIANGLES, 1, {-0.75f, 0.2f}},
    {"t1 near shared edge", Mesh::TRIANGLES, 1, {-0.6f, 0.05f}},
    {"q0", Mesh::QUADS, 0, {0.2f, -0.2f}},
    {"q0 near shared edge", Mesh::QUADS, 0, {0.45f, 0.3f}},
    {"q1", Mesh::QUADS, 1, {0.8f, -0.3f}},
    {"q1 near shared edge", Mesh::QUADS, 1, {0.55f, 0.2f}},
};

bool near(float4 a, float4 b)
{
  const float4 d = linalg::abs(a - b);
  return d.x < 1e-4f && d.y < 1e-4f && d.z < 1e-4f && d.w < 1e-4f;
}

// The scene: both meshes with 'vertex.color' VERTEX_COLOR, a matte material,
// rendered in a frame whose renderer mode the checks set
struct Scene
{
  anari::Device d;
  anari::Geometry triangles;
  anari::Geometry quads;
  anari::World world;
  anari::Renderer renderer;
  anari::Frame frame;

  explicit Scene(anari::Device d) : d(d)
  {
    const std::vector<float3> triVertexColors(
        TRI_VERTICES.size(), VERTEX_COLOR);
    triangles = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameterArray1D(d,
        triangles,
        "vertex.position",
        TRI_VERTICES.data(),
        TRI_VERTICES.size());
    anari::setParameterArray1D(d,
        triangles,
        "vertex.color",
        triVertexColors.data(),
        triVertexColors.size());
    anari::setParameterArray1D(d,
        triangles,
        "primitive.index",
        TRI_INDICES.data(),
        TRI_INDICES.size());

    const std::vector<float3> quadVertexColors(
        QUAD_VERTICES.size(), VERTEX_COLOR);
    quads = anari::newObject<anari::Geometry>(d, "quad");
    anari::setParameterArray1D(d,
        quads,
        "vertex.position",
        QUAD_VERTICES.data(),
        QUAD_VERTICES.size());
    anari::setParameterArray1D(d,
        quads,
        "vertex.color",
        quadVertexColors.data(),
        quadVertexColors.size());
    anari::setParameterArray1D(
        d, quads, "primitive.index", QUAD_INDICES.data(), QUAD_INDICES.size());

    setFaceVarying(TRI_COLORS, QUAD_COLORS);

    auto mat = anari::newObject<anari::Material>(d, "matte");
    anari::commitParameters(d, mat);
    anari::Surface surfaces[2];
    anari::Geometry geometries[2] = {triangles, quads};
    for (int i = 0; i < 2; i++) {
      surfaces[i] = anari::newObject<anari::Surface>(d);
      anari::setParameter(d, surfaces[i], "geometry", geometries[i]);
      anari::setParameter(d, surfaces[i], "material", mat);
      anari::commitParameters(d, surfaces[i]);
    }
    anari::release(d, mat);

    world = anari::newObject<anari::World>(d);
    anari::setParameterArray1D(d, world, "surface", surfaces, 2);
    anari::commitParameters(d, world);
    for (auto s : surfaces)
      anari::release(d, s);

    renderer = anari::newObject<anari::Renderer>(d, "preview");
    anari::setParameter(d, renderer, "background", BACKGROUND);
    anari::commitParameters(d, renderer);

    auto camera = anari::newObject<anari::Camera>(d, "orthographic");
    anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
    anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
    anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
    anari::setParameter(d, camera, "height", 2.f);
    anari::setParameter(d, camera, "aspect", 1.f);
    anari::commitParameters(d, camera);

    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", IMAGE_SIZE);
    anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
    anari::setAndReleaseParameter(d, frame, "camera", camera);
    anari::setParameter(d, frame, "renderer", renderer);
    anari::setParameter(d, frame, "world", world);
    anari::commitParameters(d, frame);
  }

  ~Scene()
  {
    anari::release(d, frame);
    anari::release(d, renderer);
    anari::release(d, world);
    anari::release(d, triangles);
    anari::release(d, quads);
  }

  // Sets 'faceVarying.color' to 'triColors'/'quadColors' (unset where
  // empty) and 'faceVarying.attribute0' to TRI_ATTR0/QUAD_ATTR0, and commits
  // both geometries
  void setFaceVarying(const std::vector<float3> &triColors,
      const std::vector<float3> &quadColors)
  {
    anari::Geometry geometries[2] = {triangles, quads};
    const std::vector<float3> *colors[2] = {&triColors, &quadColors};
    const std::vector<float2> *attr0[2] = {&TRI_ATTR0, &QUAD_ATTR0};
    for (int i = 0; i < 2; i++) {
      if (colors[i]->empty()) {
        anari::unsetParameter(d, geometries[i], "faceVarying.color");
      } else {
        anari::setParameterArray1D(d,
            geometries[i],
            "faceVarying.color",
            colors[i]->data(),
            colors[i]->size());
      }
      anari::setParameterArray1D(d,
          geometries[i],
          "faceVarying.attribute0",
          attr0[i]->data(),
          attr0[i]->size());
      anari::commitParameters(d, geometries[i]);
    }
  }

  std::vector<float4> render(const char *mode)
  {
    anari::setParameter(d, renderer, "mode", mode);
    anari::commitParameters(d, renderer);
    anari::render(d, frame);
    anari::wait(d, frame);
    auto fb = anari::map<float4>(d, frame, "channel.color");
    std::vector<float4> image;
    if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
      image.assign(fb.data, fb.data + size_t(IMAGE_SIZE.x) * IMAGE_SIZE.y);
    anari::unmap(d, frame, "channel.color");
    return image;
  }
};

// Renders 'mode' and checks every probe against 'expected', which gives the
// probe's value from its primitive and hit position
template <typename F>
bool checkProbes(
    Scene &scene, const char *mode, const std::string &name, F &&expected)
{
  const auto image = scene.render(mode);
  if (image.empty())
    return check(false, name + ": could not map channel.color");

  bool pass = true;
  for (const auto &p : PROBES) {
    const uint2 px = pixelAt(p.pos);
    const float4 want = expected(p, pixelCentre(px));
    const float4 got = image[size_t(px.y) * IMAGE_SIZE.x + px.x];
    if (!near(got, want)) {
      std::printf("  %s, %s: got (%f, %f, %f, %f), expected (%f, %f, %f, %f)\n",
          name.c_str(),
          p.name,
          got.x,
          got.y,
          got.z,
          got.w,
          want.x,
          want.y,
          want.z,
          want.w);
      pass = false;
    }
  }
  return check(pass, name);
}

float4 faceVaryingColor(const std::vector<float3> &triColors,
    const std::vector<float3> &quadColors,
    const Probe &p,
    float2 pos)
{
  return float4(p.mesh == Mesh::TRIANGLES
          ? triangleValue(triColors, p.prim, pos)
          : quadValue(quadColors, p.prim, pos),
      1.f);
}

float4 faceVaryingAttr0(const Probe &p, float2 pos)
{
  const float2 v = p.mesh == Mesh::TRIANGLES
      ? triangleValue(TRI_ATTR0, p.prim, pos)
      : quadValue(QUAD_ATTR0, p.prim, pos);
  // FLOAT32_VEC2 attributes read as (x, y, 0, 1)
  return float4(v.x, v.y, 0.f, 1.f);
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
  {
    Scene scene(d);

    pass &= checkProbes(scene,
        "geometry.color",
        "faceVarying.color overrides vertex.color",
        [](const Probe &p, float2 pos) {
          return faceVaryingColor(TRI_COLORS, QUAD_COLORS, p, pos);
        });
    pass &= checkProbes(scene,
        "geometry.attribute0",
        "faceVarying.attribute0",
        faceVaryingAttr0);
    pass &= check(g_warnings.empty(), "no warnings for complete arrays");

    scene.setFaceVarying({}, {});
    pass &= checkProbes(scene,
        "geometry.color",
        "unset faceVarying.color shows vertex.color",
        [](const Probe &, float2) { return float4(VERTEX_COLOR, 1.f); });

    // 4 values for the triangles' 6 corners: t1's corners 3, 4, 5 all use
    // element 3
    const std::vector<float3> shortColors(
        TRI_COLORS.begin(), TRI_COLORS.begin() + 4);
    const size_t warningsBefore = g_warnings.size();
    scene.setFaceVarying(shortColors, QUAD_COLORS);
    pass &= checkProbes(scene,
        "geometry.color",
        "short faceVarying.color clamps to its last element",
        [&](const Probe &p, float2 pos) {
          return faceVaryingColor(shortColors, QUAD_COLORS, p, pos);
        });
    const bool warnedOnce = g_warnings.size() == warningsBefore + 1
        && g_warnings.back().find("'faceVarying.color' has 4 elements")
            != std::string::npos;
    if (!warnedOnce) {
      for (size_t i = warningsBefore; i < g_warnings.size(); i++)
        std::printf("  warning: %s\n", g_warnings[i].c_str());
    }
    pass &= check(warnedOnce, "short faceVarying.color warns once");
  }

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= check(g_errors.empty(), "no device errors");
  return pass ? 0 : 1;
}
