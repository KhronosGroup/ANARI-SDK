// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Shading normals ('vertex.normal', 'faceVarying.normal') on triangle and quad
// geometry, through the device:
// - the 'Ns' mode shows 'vertex.normal' interpolated like vertex attributes
//   and normalized, and 'Ns.abs' its absolute value; 'Ng' stays the geometric
//   normal
// - 'faceVarying.normal' (one normal per primitive corner) overrides
//   'vertex.normal'; with neither set, 'Ns' shows the geometric normal
// - the 'default' mode's eye light and the 'worldNormal' attribute use the
//   shading normal
// - an instance transforms the normal by its normal matrix, while
//   'objectNormal' stays in object space
// - FIXED16_VEC3 normals read as their value / 32767
// - zero-length normals fall back to the geometric normal
// - a normal array of another element type warns and is ignored, and a short
//   'faceVarying.normal' warns and clamps to its last element

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};
const float3 NG = {0.f, 0.f, 1.f};

// The meshes of test_face_varying.cpp: triangles in x = [-0.9, -0.1] and
// quads in x = [0.1, 0.9], y = [-0.4, 0.4], all in the z = 0 plane
//   3---2        3---4---5
//   |t1/|        |q0 |q1 |
//   | /t0        0---1---2
//   0---1
const std::vector<float3> TRI_VERTICES = {{-0.9f, -0.4f, 0.f},
    {-0.1f, -0.4f, 0.f},
    {-0.1f, 0.4f, 0.f},
    {-0.9f, 0.4f, 0.f}};
const std::vector<uint3> TRI_INDICES = {{0, 1, 2}, {0, 2, 3}};
const std::vector<float3> QUAD_VERTICES = {{0.1f, -0.4f, 0.f},
    {0.5f, -0.4f, 0.f},
    {0.9f, -0.4f, 0.f},
    {0.1f, 0.4f, 0.f},
    {0.5f, 0.4f, 0.f},
    {0.9f, 0.4f, 0.f}};
const std::vector<uint4> QUAD_INDICES = {{0, 1, 4, 3}, {2, 5, 4, 1}};

// Unnormalized, with non-negative components so that 'Ns' shows them
// unclamped
const std::vector<float3> TRI_VERTEX_NORMALS = {
    {0.f, 0.f, 1.f}, {1.f, 0.f, 1.f}, {0.f, 1.f, 1.f}, {1.f, 1.f, 1.f}};
const std::vector<float3> QUAD_VERTEX_NORMALS = {{0.f, 0.f, 1.f},
    {1.f, 0.f, 1.f},
    {0.5f, 0.f, 1.f},
    {0.f, 1.f, 1.f},
    {1.f, 1.f, 1.f},
    {0.2f, 0.3f, 1.f}};
// One per corner, some with negative components (checked with 'Ns.abs')
const std::vector<float3> TRI_FACE_VARYING_NORMALS = {{-1.f, 0.f, 1.f},
    {0.f, -1.f, 1.f},
    {1.f, 1.f, 2.f},
    {0.5f, -0.5f, 1.f},
    {0.f, 0.f, 1.f},
    {-0.3f, 0.8f, 1.f}};
const std::vector<float3> QUAD_FACE_VARYING_NORMALS = {{-1.f, 0.f, 1.f},
    {0.f, -1.f, 1.f},
    {1.f, 0.f, 1.f},
    {0.f, 1.f, 1.f},
    {-0.5f, 0.5f, 1.f},
    {0.5f, -0.5f, 1.f},
    {0.2f, 0.2f, 1.f},
    {-0.2f, -0.2f, 1.f}};

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

enum class Mesh
{
  TRIANGLES,
  QUADS
};

// Where the normals come from
enum class Source
{
  VERTEX,
  FACE_VARYING
};

struct Probe
{
  const char *name;
  Mesh mesh;
  uint32_t prim;
  float2 pos;
};

// Two probes per primitive, the ones of a mesh's primitives near their shared
// edge, where the corners share vertices but not face-varying normals
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

// Normal of probe 'p' at 'pos', unnormalized, interpolated like attributes
// from 'triNormals'/'quadNormals' (by vertex or by corner, per 'source';
// arrays shorter than needed clamp to their last element): barycentric in a
// triangle's corners (Embree's (1 - u - v, u, v)), bilinear in a quad's
// corners at (u, v) = (0, 0), (1, 0), (1, 1), (0, 1)
float3 interpolatedNormal(const Probe &p,
    float2 pos,
    Source source,
    const std::vector<float3> &triNormals,
    const std::vector<float3> &quadNormals)
{
  const bool tri = p.mesh == Mesh::TRIANGLES;
  const std::vector<float3> &normals = tri ? triNormals : quadNormals;
  const uint32_t cornersPerPrim = tri ? 3 : 4;
  auto corner = [&](uint32_t c, uint32_t vertex) {
    const size_t i =
        source == Source::VERTEX ? vertex : cornersPerPrim * p.prim + c;
    return normals[std::min(i, normals.size() - 1)];
  };

  if (tri) {
    const uint3 idx = TRI_INDICES[p.prim];
    const float2 a = xy(TRI_VERTICES[idx.x]);
    const float2 e1 = xy(TRI_VERTICES[idx.y]) - a;
    const float2 e2 = xy(TRI_VERTICES[idx.z]) - a;
    const float2 d = pos - a;
    const float det = e1.x * e2.y - e1.y * e2.x;
    const float u = (d.x * e2.y - d.y * e2.x) / det;
    const float v = (e1.x * d.y - e1.y * d.x) / det;
    return (1.f - u - v) * corner(0, idx.x) + u * corner(1, idx.y)
        + v * corner(2, idx.z);
  }

  const uint4 idx = QUAD_INDICES[p.prim];
  const float2 a = xy(QUAD_VERTICES[idx.x]);
  const float2 eu = xy(QUAD_VERTICES[idx.y]) - a;
  const float2 ev = xy(QUAD_VERTICES[idx.w]) - a;
  // the quads are rectangles, so (u, v) are projections onto the edges
  const float u = linalg::dot(pos - a, eu) / linalg::dot(eu, eu);
  const float v = linalg::dot(pos - a, ev) / linalg::dot(ev, ev);
  return (1.f - v) * (1.f - u) * corner(0, idx.x)
      + (1.f - v) * u * corner(1, idx.y) + v * u * corner(2, idx.z)
      + v * (1.f - u) * corner(3, idx.w);
}

bool near(float4 a, float4 b)
{
  const float4 d = linalg::abs(a - b);
  return d.x < 1e-4f && d.y < 1e-4f && d.z < 1e-4f && d.w < 1e-4f;
}

// Sets normal array parameter 'name' of 'g' to 'normals' (unset if empty)
void setNormals(anari::Device d,
    anari::Geometry g,
    const char *name,
    const std::vector<float3> &normals)
{
  if (normals.empty())
    anari::unsetParameter(d, g, name);
  else
    anari::setParameterArray1D(d, g, name, normals.data(), normals.size());
}

// The scene: both meshes with a matte material whose color the checks set,
// in one instance whose transform the checks set, rendered in a frame whose
// renderer mode the checks set; eyeLightBlendRatio 1, so the default mode
// shows the material color times the facing ratio
struct Scene
{
  anari::Device d;
  anari::Geometry geometries[2];
  anari::Material material;
  anari::Instance instance;
  anari::Renderer renderer;
  anari::Frame frame;

  explicit Scene(anari::Device d) : d(d)
  {
    geometries[0] = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameterArray1D(d,
        geometries[0],
        "vertex.position",
        TRI_VERTICES.data(),
        TRI_VERTICES.size());
    anari::setParameterArray1D(d,
        geometries[0],
        "primitive.index",
        TRI_INDICES.data(),
        TRI_INDICES.size());

    geometries[1] = anari::newObject<anari::Geometry>(d, "quad");
    anari::setParameterArray1D(d,
        geometries[1],
        "vertex.position",
        QUAD_VERTICES.data(),
        QUAD_VERTICES.size());
    anari::setParameterArray1D(d,
        geometries[1],
        "primitive.index",
        QUAD_INDICES.data(),
        QUAD_INDICES.size());
    for (auto g : geometries)
      anari::commitParameters(d, g);

    // white, so the eye-lit colour is the facing ratio
    material = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, material, "color", float3(1.f));
    anari::commitParameters(d, material);
    anari::Surface surfaces[2];
    for (int i = 0; i < 2; i++) {
      surfaces[i] = anari::newObject<anari::Surface>(d);
      anari::setParameter(d, surfaces[i], "geometry", geometries[i]);
      anari::setParameter(d, surfaces[i], "material", material);
      anari::commitParameters(d, surfaces[i]);
    }

    auto group = anari::newObject<anari::Group>(d);
    anari::setParameterArray1D(d, group, "surface", surfaces, 2);
    anari::commitParameters(d, group);
    for (auto s : surfaces)
      anari::release(d, s);

    instance = anari::newObject<anari::Instance>(d, "transform");
    anari::setAndReleaseParameter(d, instance, "group", group);
    anari::commitParameters(d, instance);

    auto world = anari::newObject<anari::World>(d);
    anari::setParameterArray1D(d, world, "instance", &instance, 1);
    anari::commitParameters(d, world);

    renderer = anari::newObject<anari::Renderer>(d, "preview");
    anari::setParameter(d, renderer, "background", BACKGROUND);
    anari::setParameter(d, renderer, "eyeLightBlendRatio", 1.f);
    // The expected colours are those of the eye light alone, without ambient
    // occlusion (on by default)
    anari::setParameter(d, renderer, "ambientOcclusion", false);
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
    anari::setAndReleaseParameter(d, frame, "world", world);
    anari::commitParameters(d, frame);
  }

  ~Scene()
  {
    anari::release(d, frame);
    anari::release(d, renderer);
    anari::release(d, instance);
    anari::release(d, material);
    for (auto g : geometries)
      anari::release(d, g);
  }

  // Sets 'vertex.normal' and 'faceVarying.normal' of both meshes (unset
  // where empty) and commits them
  void setNormals(const std::vector<float3> &triVertex,
      const std::vector<float3> &quadVertex,
      const std::vector<float3> &triFaceVarying,
      const std::vector<float3> &quadFaceVarying)
  {
    ::setNormals(d, geometries[0], "vertex.normal", triVertex);
    ::setNormals(d, geometries[1], "vertex.normal", quadVertex);
    ::setNormals(d, geometries[0], "faceVarying.normal", triFaceVarying);
    ::setNormals(d, geometries[1], "faceVarying.normal", quadFaceVarying);
    for (auto g : geometries)
      anari::commitParameters(d, g);
  }

  // Sets the matte material's 'color' to attribute 'attribute', or to white
  // if null
  void setColorAttribute(const char *attribute)
  {
    if (attribute)
      anari::setParameter(d, material, "color", attribute);
    else
      anari::setParameter(d, material, "color", float3(1.f));
    anari::commitParameters(d, material);
  }

  void setTransform(const mat4 &xfm)
  {
    anari::setParameter(d, instance, "transform", xfm);
    anari::commitParameters(d, instance);
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

using Expected = std::function<float4(const Probe &, float2)>;

// Renders 'mode' and checks every probe against 'expected', which gives the
// probe's value from its primitive and hit position
bool checkProbes(Scene &scene,
    const char *mode,
    const std::string &name,
    const Expected &expected)
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

// The normalized normal of each probe from normals 'tri'/'quad' ('source'),
// passed through 'f' (world transform, abs, facing ratio, ...)
Expected normals(
    Source source,
    const std::vector<float3> &tri,
    const std::vector<float3> &quad,
    std::function<float4(float3)> f = [](float3 n) { return float4(n, 1.f); })
{
  return [=](const Probe &p, float2 pos) {
    return f(linalg::normalize(interpolatedNormal(p, pos, source, tri, quad)));
  };
}

float4 absNormal(float3 n)
{
  return float4(linalg::abs(n), 1.f);
}

// The default mode's color for a white material, eyeLightBlendRatio 1 and an
// orthographic camera looking down -z: the facing ratio |n.z|
float4 eyeLit(float3 n)
{
  return float4(float3(std::fabs(n.z)), 1.f);
}

// Checks that setting normal array 'name' to an array of 'type' warns once
// that it is ignored, and 'Ns' shows the geometric normal
bool checkIgnoredType(Scene &scene,
    const char *name,
    anari::DataType type,
    const std::string &checkName)
{
  auto d = scene.d;
  const size_t count = std::string(name) == "vertex.normal" ? 4 : 6;
  auto array = anari::newArray1D(d, type, count);
  auto *bytes = static_cast<uint8_t *>(anari::map<void>(d, array));
  std::fill(bytes, bytes + count * anari::sizeOf(type), uint8_t(0));
  anari::unmap(d, array);

  scene.setNormals({}, {}, {}, {});
  const size_t warningsBefore = g_warnings.size();
  anari::setAndReleaseParameter(d, scene.geometries[0], name, array);
  anari::commitParameters(d, scene.geometries[0]);
  bool pass = checkProbes(
      scene, "Ns", checkName + " shows Ng", [](const Probe &, float2) {
        return float4(NG, 1.f);
      });
  const std::string expected =
      std::string("'") + name + "' array elements are " + anari::toString(type);
  const bool warnedOnce = g_warnings.size() == warningsBefore + 1
      && g_warnings.back().find(expected) != std::string::npos;
  if (!warnedOnce) {
    for (size_t i = warningsBefore; i < g_warnings.size(); i++)
      std::printf("  warning: %s\n", g_warnings[i].c_str());
  }
  pass &= check(warnedOnce, checkName + " warns once");
  anari::unsetParameter(d, scene.geometries[0], name);
  anari::commitParameters(d, scene.geometries[0]);
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

  bool pass = true;
  {
    Scene scene(d);

    pass &= checkProbes(
        scene, "Ns", "Ns without normals shows Ng", [](const Probe &, float2) {
          return float4(NG, 1.f);
        });

    scene.setNormals(TRI_VERTEX_NORMALS, QUAD_VERTEX_NORMALS, {}, {});
    pass &= checkProbes(scene,
        "Ns",
        "Ns interpolates vertex.normal",
        normals(Source::VERTEX, TRI_VERTEX_NORMALS, QUAD_VERTEX_NORMALS));
    pass &= checkProbes(
        scene, "Ng", "Ng ignores vertex.normal", [](const Probe &, float2) {
          return float4(NG, 1.f);
        });
    pass &= checkProbes(scene,
        "default",
        "default mode eye light uses vertex.normal",
        normals(
            Source::VERTEX, TRI_VERTEX_NORMALS, QUAD_VERTEX_NORMALS, eyeLit));
    // the attribute as color, times the facing ratio of the same normal
    scene.setColorAttribute("worldNormal");
    pass &= checkProbes(scene,
        "default",
        "worldNormal attribute is the shading normal",
        normals(Source::VERTEX,
            TRI_VERTEX_NORMALS,
            QUAD_VERTEX_NORMALS,
            [](float3 n) { return float4(n * std::fabs(n.z), 1.f); }));

    // a scale in z keeps the meshes (z = 0) in place but tilts their normals
    // by the normal matrix diag(1, 1, 1/2)
    scene.setTransform(mat4({1.f, 0.f, 0.f, 0.f},
        {0.f, 1.f, 0.f, 0.f},
        {0.f, 0.f, 2.f, 0.f},
        {0.f, 0.f, 0.f, 1.f}));
    auto scaled = [](float3 n) {
      return float4(linalg::normalize(n * float3(1.f, 1.f, 0.5f)), 1.f);
    };
    pass &= checkProbes(scene,
        "Ns",
        "Ns transforms vertex.normal by the instance's normal matrix",
        normals(
            Source::VERTEX, TRI_VERTEX_NORMALS, QUAD_VERTEX_NORMALS, scaled));
    pass &= checkProbes(scene,
        "default",
        "worldNormal attribute transforms by the normal matrix",
        normals(Source::VERTEX,
            TRI_VERTEX_NORMALS,
            QUAD_VERTEX_NORMALS,
            [&](float3 n) {
              const float4 w = scaled(n);
              return float4(float3(w.x, w.y, w.z) * std::fabs(w.z), 1.f);
            }));
    scene.setColorAttribute("objectNormal");
    pass &= checkProbes(scene,
        "default",
        "objectNormal attribute stays in object space",
        normals(Source::VERTEX,
            TRI_VERTEX_NORMALS,
            QUAD_VERTEX_NORMALS,
            [&](float3 n) {
              const float4 w = scaled(n);
              return float4(n * std::fabs(w.z), 1.f);
            }));
    scene.setTransform(mat4(linalg::identity));
    scene.setColorAttribute(nullptr);

    scene.setNormals(TRI_VERTEX_NORMALS,
        QUAD_VERTEX_NORMALS,
        TRI_FACE_VARYING_NORMALS,
        QUAD_FACE_VARYING_NORMALS);
    pass &= checkProbes(scene,
        "Ns.abs",
        "faceVarying.normal overrides vertex.normal",
        normals(Source::FACE_VARYING,
            TRI_FACE_VARYING_NORMALS,
            QUAD_FACE_VARYING_NORMALS,
            absNormal));
    pass &= checkProbes(scene,
        "default",
        "default mode eye light uses faceVarying.normal",
        normals(Source::FACE_VARYING,
            TRI_FACE_VARYING_NORMALS,
            QUAD_FACE_VARYING_NORMALS,
            eyeLit));

    scene.setNormals(
        {}, {}, TRI_FACE_VARYING_NORMALS, QUAD_FACE_VARYING_NORMALS);
    pass &= checkProbes(scene,
        "Ns.abs",
        "faceVarying.normal without vertex.normal",
        normals(Source::FACE_VARYING,
            TRI_FACE_VARYING_NORMALS,
            QUAD_FACE_VARYING_NORMALS,
            absNormal));
    pass &= check(g_warnings.empty(), "no warnings for complete arrays");

    scene.setNormals({}, {}, {}, {});
    pass &= checkProbes(
        scene, "Ns", "unset normals show Ng again", [](const Probe &, float2) {
          return float4(NG, 1.f);
        });

    // FIXED16_VEC3: the triangles' vertex normals as values / 32767
    {
      const std::vector<int16_t> fixed = {
          0, 0, 32767, 32767, 0, 32767, 0, 16384, 32767, -8192, 32767, 32767};
      std::vector<float3> asFloat;
      for (size_t i = 0; i < fixed.size(); i += 3) {
        asFloat.push_back(
            float3(fixed[i], fixed[i + 1], fixed[i + 2]) / 32767.f);
      }
      auto array = anari::newArray1D(d, ANARI_FIXED16_VEC3, 4);
      auto *values = anari::map<int16_t>(d, array);
      std::copy(fixed.begin(), fixed.end(), values);
      anari::unmap(d, array);
      anari::setAndReleaseParameter(
          d, scene.geometries[0], "vertex.normal", array);
      anari::commitParameters(d, scene.geometries[0]);
      pass &= checkProbes(scene,
          "Ns.abs",
          "FIXED16_VEC3 vertex.normal",
          [&](const Probe &p, float2 pos) {
            if (p.mesh == Mesh::QUADS)
              return float4(NG, 1.f);
            return absNormal(linalg::normalize(interpolatedNormal(
                p, pos, Source::VERTEX, asFloat, QUAD_VERTEX_NORMALS)));
          });
    }

    // zero normals everywhere: every probe falls back to Ng
    scene.setNormals(std::vector<float3>(4, float3(0.f)),
        {},
        {},
        std::vector<float3>(8, float3(0.f)));
    pass &= checkProbes(scene,
        "Ns",
        "zero-length normals fall back to Ng",
        [](const Probe &, float2) { return float4(NG, 1.f); });
    pass &= check(g_warnings.empty(), "no warnings before the warning checks");

    pass &= checkIgnoredType(scene,
        "vertex.normal",
        ANARI_FLOAT32_VEC4,
        "FLOAT32_VEC4 vertex.normal");
    pass &= checkIgnoredType(scene,
        "faceVarying.normal",
        ANARI_FLOAT32_VEC2,
        "FLOAT32_VEC2 faceVarying.normal");

    // 4 normals for the triangles' 6 corners: t1's corners 3, 4, 5 all use
    // element 3
    const std::vector<float3> shortNormals(
        TRI_FACE_VARYING_NORMALS.begin(), TRI_FACE_VARYING_NORMALS.begin() + 4);
    const size_t warningsBefore = g_warnings.size();
    scene.setNormals({}, {}, shortNormals, QUAD_FACE_VARYING_NORMALS);
    pass &= checkProbes(scene,
        "Ns.abs",
        "short faceVarying.normal clamps to its last element",
        normals(Source::FACE_VARYING,
            shortNormals,
            QUAD_FACE_VARYING_NORMALS,
            absNormal));
    const bool warnedOnce = g_warnings.size() == warningsBefore + 1
        && g_warnings.back().find("'faceVarying.normal' has 4 elements")
            != std::string::npos;
    if (!warnedOnce) {
      for (size_t i = warningsBefore; i < g_warnings.size(); i++)
        std::printf("  warning: %s\n", g_warnings[i].c_str());
    }
    pass &= check(warnedOnce, "short faceVarying.normal warns once");
  }

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= check(g_errors.empty(), "no device errors");
  return pass ? 0 : 1;
}
