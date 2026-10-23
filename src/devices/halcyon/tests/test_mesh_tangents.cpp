// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The tangent frame of triangle and quad meshes in the 'quality' renderer's
// shading context (its 'tangent' and 'bitangent' debug modes), through the
// device:
// - without authored tangents, the tangents Typhoon smooths across the faces
//   that share a vertex and its normal: on a coarse latitude-longitude sphere
//   (triangles and quads) with smooth normals and texture coordinates, the
//   frame follows the sphere's own (east, north) within the tessellation's
//   error, where per-face tangents don't
// - 'vertex.tangent' (FLOAT32_VEC3, FLOAT32_VEC4 with the handedness in w,
//   FIXED16_VEC4) interpolated like vertex attributes, the bitangent
//   w * N x tangent; 'faceVarying.tangent' (one per primitive corner)
//   overrides it unless it is empty; through an instance transform
// - a tangent array of another element type warns and is ignored, and a
//   short 'faceVarying.tangent' warns and clamps to its last element
// - the 'preview' renderer renders the same image with or without tangents

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};
constexpr float PI = 3.14159265358979323846f;
const float3 Z = {0.f, 0.f, 1.f};

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

// The pixel covering (x, y) of the orthographic [-1, 1]^2 view
uint2 pixelAt(float2 pos)
{
  return uint2(uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x),
      uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y));
}

// The (x, y) of a pixel's centre, where its ray goes
float2 pixelCentre(uint2 px)
{
  return float2(2.f * (px.x + 0.5f) / IMAGE_SIZE.x - 1.f,
      2.f * (px.y + 0.5f) / IMAGE_SIZE.y - 1.f);
}

// Scenes ////////////////////////////////////////////////////////////////////

// Orthographic camera at z = 2 looking down -z at the [-1, 1]^2 square
anari::Camera orthographicCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

// A world of geometry 'geom' with a matte material, in an instance with
// transform 'xfm'
anari::World worldOf(anari::Device d, anari::Geometry geom, mat4 xfm)
{
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "surface", &surface, 1);
  anari::commitParameters(d, group);
  anari::release(d, surface);
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

// Renders 'world' with 'renderer' in 'mode' and returns the colour channel
std::vector<float4> render(anari::Device d,
    anari::World world,
    const char *rendererType,
    const char *mode)
{
  auto camera = orthographicCamera(d);
  auto renderer = anari::newObject<anari::Renderer>(d, rendererType);
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  anari::setParameter(d, renderer, "mode", mode);
  anari::commitParameters(d, renderer);
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);
  auto fb = anari::map<float4>(d, frame, "channel.color");
  std::vector<float4> pixels(fb.data, fb.data + IMAGE_SIZE.x * IMAGE_SIZE.y);
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);
  return pixels;
}

float3 xyz(float4 v)
{
  return float3(v.x, v.y, v.z);
}

float4 pixel(const std::vector<float4> &image, uint2 px)
{
  return image[size_t(px.y) * IMAGE_SIZE.x + px.x];
}

bool near(float3 a, float3 b, float tolerance)
{
  for (int i = 0; i < 3; i++) {
    if (!(std::abs(a[i] - b[i]) <= tolerance))
      return false;
  }
  return true;
}

// The expected tangent frame at a probe pixel
struct Probe
{
  uint2 pixel;
  float3 tangent;
  float3 bitangent;
};

// Renders the 'tangent' and 'bitangent' modes of 'geom' (in an instance with
// transform 'xfm') and compares them with 'probes'
bool checkFrame(anari::Device d,
    anari::Geometry geom,
    const std::vector<Probe> &probes,
    float tolerance,
    const std::string &name,
    mat4 xfm = linalg::identity)
{
  anari::commitParameters(d, geom);
  auto world = worldOf(d, geom, xfm);
  const auto tangents = render(d, world, "quality", "tangent");
  const auto bitangents = render(d, world, "quality", "bitangent");
  anari::release(d, world);

  bool pass = true;
  for (const Probe &p : probes) {
    const float3 t = xyz(pixel(tangents, p.pixel));
    const float3 b = xyz(pixel(bitangents, p.pixel));
    if (!near(t, p.tangent, tolerance) || !near(b, p.bitangent, tolerance)) {
      std::printf(
          "  at (%u, %u): tangent (%g, %g, %g), expected (%g, %g, %g); "
          "bitangent (%g, %g, %g), expected (%g, %g, %g)\n",
          p.pixel.x,
          p.pixel.y,
          t.x,
          t.y,
          t.z,
          p.tangent.x,
          p.tangent.y,
          p.tangent.z,
          b.x,
          b.y,
          b.z,
          p.bitangent.x,
          p.bitangent.y,
          p.bitangent.z);
      pass = false;
    }
  }
  return check(pass, name);
}

// Smooth sphere //////////////////////////////////////////////////////////////

// A latitude-longitude sphere of radius R around the y axis: longitude phi
// (texture s = phi / 2pi) with its seam at the back (z < 0), latitude theta
// (t = theta / pi) from the -y pole to the +y pole, so that dP/ds x dP/dt
// points out
constexpr float R = 0.9f;
constexpr int SEGMENTS = 24; // around
constexpr int RINGS = 12; // pole to pole

float3 spherePoint(float phi, float theta)
{
  return R
      * float3(-std::sin(theta) * std::sin(phi),
          -std::cos(theta),
          -std::sin(theta) * std::cos(phi));
}

// The sphere tessellated into SEGMENTS x RINGS cells of two triangles or one
// quad, with its normals and texture coordinates per vertex; the seam's
// vertices are duplicated, so that s goes from 0 to 1
anari::Geometry tessellatedSphere(anari::Device d, bool quads)
{
  std::vector<float3> positions, normals;
  std::vector<float2> st;
  for (int j = 0; j <= RINGS; j++) {
    for (int i = 0; i <= SEGMENTS; i++) {
      const float s = float(i) / SEGMENTS;
      const float t = float(j) / RINGS;
      const float3 p = spherePoint(2.f * PI * s, PI * t);
      positions.push_back(p);
      normals.push_back(p / R);
      st.push_back(float2(s, t));
    }
  }
  auto vertex = [](int i, int j) { return uint32_t(j * (SEGMENTS + 1) + i); };
  std::vector<uint3> triangles;
  std::vector<uint4> cells;
  for (int j = 0; j < RINGS; j++) {
    for (int i = 0; i < SEGMENTS; i++) {
      const uint4 c(vertex(i, j),
          vertex(i + 1, j),
          vertex(i + 1, j + 1),
          vertex(i, j + 1));
      cells.push_back(c);
      triangles.push_back(uint3(c.x, c.y, c.z));
      triangles.push_back(uint3(c.x, c.z, c.w));
    }
  }

  auto geom = anari::newObject<anari::Geometry>(d, quads ? "quad" : "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", positions.data(), positions.size());
  anari::setParameterArray1D(
      d, geom, "vertex.normal", normals.data(), normals.size());
  anari::setParameterArray1D(
      d, geom, "vertex.attribute0", st.data(), st.size());
  if (quads) {
    anari::setParameterArray1D(
        d, geom, "primitive.index", cells.data(), cells.size());
  } else {
    anari::setParameterArray1D(
        d, geom, "primitive.index", triangles.data(), triangles.size());
  }
  return geom;
}

// Pixels whose rays hit the front of the sphere away from its poles and seam,
// with the sphere's own frame in the direction of the (object-space) hit
// positions 'hits' from its centre: dP/ds and dP/dt are orthogonal there, so
// the tangent is the unit dP/ds (east) and the bitangent the unit dP/dt
// (north)
std::vector<Probe> sphereProbes(const std::vector<float4> &hits)
{
  std::vector<Probe> probes;
  for (float2 xy : {float2(0.3f, 0.4f),
           float2(-0.45f, 0.2f),
           float2(0.2f, -0.5f),
           float2(-0.3f, -0.35f),
           float2(0.55f, 0.1f),
           float2(0.05f, 0.6f),
           float2(-0.6f, -0.1f),
           float2(0.4f, -0.3f)}) {
    Probe p;
    p.pixel = pixelAt(xy);
    const float3 h = xyz(pixel(hits, p.pixel));
    const float theta = std::acos(-h.y / linalg::length(h));
    const float phi = std::atan2(-h.x, -h.z) + 2.f * PI;
    p.tangent = float3(-std::cos(phi), 0.f, std::sin(phi));
    p.bitangent = float3(-std::cos(theta) * std::sin(phi),
        std::sin(theta),
        -std::cos(theta) * std::cos(phi));
    probes.push_back(p);
  }
  return probes;
}

// Typhoon's smoothed tangents on the sphere: within the tessellation's error
// of the sphere's frame (below 0.01 at these probes, where per-face tangents
// are off by up to 0.05)
bool testSmoothSphere(anari::Device d)
{
  const float tolerance = 0.015f;
  bool pass = true;
  for (bool quads : {false, true}) {
    auto geom = tessellatedSphere(d, quads);
    anari::commitParameters(d, geom);
    auto world = worldOf(d, geom, linalg::identity);
    const auto hits = render(d, world, "quality", "position");
    anari::release(d, world);
    pass &= checkFrame(d,
        geom,
        sphereProbes(hits),
        tolerance,
        std::string("smoothed tangents on a ") + (quads ? "quad" : "triangle")
            + " sphere");
    anari::release(d, geom);
  }
  return pass;
}

// Authored tangents //////////////////////////////////////////////////////////

// The meshes of test_shading_normals.cpp: triangles in x = [-0.9, -0.1] and
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

// A tangent in the z = 0 plane at 'degrees' from +x, of length 'length'
float3 inPlane(float degrees, float length)
{
  const float a = degrees * PI / 180.f;
  return length * float3(std::cos(a), std::sin(a), 0.f);
}

// One per vertex of each mesh, of different lengths (interpolated as given)
const std::vector<float3> TRI_TANGENTS = {inPlane(0.f, 1.f),
    inPlane(30.f, 2.f),
    inPlane(60.f, 0.5f),
    inPlane(100.f, 1.f)};
const std::vector<float3> QUAD_TANGENTS = {inPlane(-20.f, 1.f),
    inPlane(10.f, 1.5f),
    inPlane(40.f, 1.f),
    inPlane(70.f, 0.7f),
    inPlane(15.f, 1.f),
    inPlane(-45.f, 2.f)};
// One per triangle corner
const std::vector<float3> TRI_FACE_VARYING_TANGENTS = {inPlane(170.f, 1.f),
    inPlane(140.f, 1.f),
    inPlane(120.f, 2.f),
    inPlane(-90.f, 1.f),
    inPlane(-60.f, 0.5f),
    inPlane(-30.f, 1.f)};

// A probe on a primitive: its pixel, the primitive's corners (vertex indices
// and corner numbers) and their weights there
struct MeshProbe
{
  uint2 pixel;
  std::vector<uint32_t> vertices;
  std::vector<uint32_t> corners;
  std::vector<float> weights;
};

// Embree's barycentric weights of 'p' in triangle (a, b, c)
std::vector<float> barycentric(float2 p, float2 a, float2 b, float2 c)
{
  const float2 e1 = b - a, e2 = c - a, q = p - a;
  const float det = e1.x * e2.y - e1.y * e2.x;
  const float u = (q.x * e2.y - q.y * e2.x) / det;
  const float v = (e1.x * q.y - e1.y * q.x) / det;
  return {1.f - u - v, u, v};
}

float2 xy(float3 v)
{
  return float2(v.x, v.y);
}

std::vector<MeshProbe> triangleProbes()
{
  std::vector<MeshProbe> probes;
  for (auto [pos, t] : {std::pair{float2(-0.3f, -0.2f), 0u},
           std::pair{float2(-0.5f, -0.3f), 0u},
           std::pair{float2(-0.7f, 0.2f), 1u},
           std::pair{float2(-0.6f, -0.05f), 1u}}) {
    MeshProbe p;
    p.pixel = pixelAt(pos);
    const uint3 idx = TRI_INDICES[t];
    p.vertices = {idx.x, idx.y, idx.z};
    p.corners = {3 * t, 3 * t + 1, 3 * t + 2};
    p.weights = barycentric(pixelCentre(p.pixel),
        xy(TRI_VERTICES[idx.x]),
        xy(TRI_VERTICES[idx.y]),
        xy(TRI_VERTICES[idx.z]));
    probes.push_back(p);
  }
  return probes;
}

std::vector<MeshProbe> quadProbes()
{
  std::vector<MeshProbe> probes;
  for (float2 pos : {float2(0.3f, -0.2f),
           float2(0.2f, 0.25f),
           float2(0.7f, 0.1f),
           float2(0.8f, -0.3f)}) {
    MeshProbe p;
    p.pixel = pixelAt(pos);
    const float2 c = pixelCentre(p.pixel);
    // the bilinear (u, v) of halcyon's quads: q0 from vertex 0 along +x
    // and +y, q1 from vertex 2 along +y and -x
    const uint32_t q = c.x < 0.5f ? 0 : 1;
    const float u = q == 0 ? (c.x - 0.1f) / 0.4f : (c.y + 0.4f) / 0.8f;
    const float v = q == 0 ? (c.y + 0.4f) / 0.8f : (0.9f - c.x) / 0.4f;
    const uint4 idx = QUAD_INDICES[q];
    p.vertices = {idx.x, idx.y, idx.z, idx.w};
    p.corners = {4 * q, 4 * q + 1, 4 * q + 2, 4 * q + 3};
    p.weights = {(1.f - u) * (1.f - v), u * (1.f - v), u * v, (1.f - u) * v};
    probes.push_back(p);
  }
  return probes;
}

// The frame of a flat mesh (normal +z) whose corners have tangents 'cornerT'
// and handedness 'w': the interpolated tangent, normalized, and w N x tangent
Probe authoredFrame(const MeshProbe &mp,
    const std::vector<float3> &tangents,
    bool faceVarying,
    float w)
{
  float3 t(0.f);
  for (size_t i = 0; i < mp.weights.size(); i++) {
    const uint32_t e = faceVarying ? mp.corners[i] : mp.vertices[i];
    t += mp.weights[i] * tangents[std::min<size_t>(e, tangents.size() - 1)];
  }
  Probe p;
  p.pixel = mp.pixel;
  p.tangent = linalg::normalize(t);
  p.bitangent = w * linalg::cross(Z, p.tangent);
  return p;
}

std::vector<Probe> authoredFrames(const std::vector<MeshProbe> &mps,
    const std::vector<float3> &tangents,
    bool faceVarying,
    float w)
{
  std::vector<Probe> probes;
  for (const MeshProbe &mp : mps)
    probes.push_back(authoredFrame(mp, tangents, faceVarying, w));
  return probes;
}

anari::Geometry flatMesh(anari::Device d, bool quads)
{
  auto geom = anari::newObject<anari::Geometry>(d, quads ? "quad" : "triangle");
  const auto &v = quads ? QUAD_VERTICES : TRI_VERTICES;
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  if (quads) {
    anari::setParameterArray1D(
        d, geom, "primitive.index", QUAD_INDICES.data(), QUAD_INDICES.size());
  } else {
    anari::setParameterArray1D(
        d, geom, "primitive.index", TRI_INDICES.data(), TRI_INDICES.size());
  }
  return geom;
}

// 'tangents' as FLOAT32_VEC4 with handedness 'w'
std::vector<float4> withW(const std::vector<float3> &tangents, float w)
{
  std::vector<float4> r;
  for (const float3 &t : tangents)
    r.push_back(float4(t, w));
  return r;
}

// 'tangents' (components in [-1, 1]) as FIXED16_VEC4 with handedness 'w',
// and the values they decode to
anari::Array1D fixed16Tangents(anari::Device d,
    const std::vector<float3> &tangents,
    float w,
    std::vector<float3> &decoded)
{
  auto array = anari::newArray1D(d, ANARI_FIXED16_VEC4, tangents.size());
  auto *values = static_cast<int16_t *>(anari::map<void>(d, array));
  decoded.clear();
  for (size_t i = 0; i < tangents.size(); i++) {
    float3 dec;
    for (int c = 0; c < 3; c++) {
      const int16_t q = int16_t(std::lround(tangents[i][c] * 32767.f));
      values[4 * i + c] = q;
      dec[c] = q / 32767.f;
    }
    values[4 * i + 3] = int16_t(w * 32767.f);
    decoded.push_back(dec);
  }
  anari::unmap(d, array);
  return array;
}

bool warnedOnce(size_t before, const std::string &expected);

bool testAuthored(anari::Device d)
{
  // the probes' frames are exact up to float rounding
  const float tolerance = 1e-4f;
  bool pass = true;
  const auto triProbes = triangleProbes();
  const auto qProbes = quadProbes();

  {
    auto geom = flatMesh(d, false);
    anari::setParameterArray1D(
        d, geom, "vertex.tangent", TRI_TANGENTS.data(), TRI_TANGENTS.size());
    pass &= checkFrame(d,
        geom,
        authoredFrames(triProbes, TRI_TANGENTS, false, 1.f),
        tolerance,
        "FLOAT32_VEC3 'vertex.tangent' on triangles");

    const auto flipped = withW(TRI_TANGENTS, -1.f);
    anari::setParameterArray1D(
        d, geom, "vertex.tangent", flipped.data(), flipped.size());
    pass &= checkFrame(d,
        geom,
        authoredFrames(triProbes, TRI_TANGENTS, false, -1.f),
        tolerance,
        "FLOAT32_VEC4 'vertex.tangent' with w = -1 flips the bitangent");

    std::vector<float3> unit;
    for (const float3 &t : TRI_TANGENTS)
      unit.push_back(linalg::normalize(t));
    std::vector<float3> decoded;
    anari::setAndReleaseParameter(
        d, geom, "vertex.tangent", fixed16Tangents(d, unit, -1.f, decoded));
    pass &= checkFrame(d,
        geom,
        authoredFrames(triProbes, decoded, false, -1.f),
        tolerance,
        "FIXED16_VEC4 'vertex.tangent' reads value / 32767");

    anari::setParameterArray1D(d,
        geom,
        "faceVarying.tangent",
        TRI_FACE_VARYING_TANGENTS.data(),
        TRI_FACE_VARYING_TANGENTS.size());
    pass &= checkFrame(d,
        geom,
        authoredFrames(triProbes, TRI_FACE_VARYING_TANGENTS, true, 1.f),
        tolerance,
        "'faceVarying.tangent' overrides 'vertex.tangent' on triangles");

    // an empty 'faceVarying.tangent' leaves 'vertex.tangent' in effect (and
    // warns that it is short, as for the other face-varying arrays)
    const size_t warningsBefore = g_warnings.size();
    anari::setParameterArray1D(
        d, geom, "vertex.tangent", TRI_TANGENTS.data(), TRI_TANGENTS.size());
    anari::setAndReleaseParameter(d,
        geom,
        "faceVarying.tangent",
        anari::newArray1D(d, ANARI_FLOAT32_VEC3, 0));
    pass &= checkFrame(d,
        geom,
        authoredFrames(triProbes, TRI_TANGENTS, false, 1.f),
        tolerance,
        "an empty 'faceVarying.tangent' leaves 'vertex.tangent'");
    pass &= check(
        warnedOnce(warningsBefore, "'faceVarying.tangent' has 0 elements"),
        "an empty 'faceVarying.tangent' warns that it is short");
    g_warnings.resize(warningsBefore);
    anari::release(d, geom);
  }

  {
    auto geom = flatMesh(d, true);
    anari::setParameterArray1D(
        d, geom, "vertex.tangent", QUAD_TANGENTS.data(), QUAD_TANGENTS.size());
    pass &= checkFrame(d,
        geom,
        authoredFrames(qProbes, QUAD_TANGENTS, false, 1.f),
        tolerance,
        "'vertex.tangent' on quads");

    // eight corners, one value per quad corner
    std::vector<float3> fv;
    for (int i = 0; i < 8; i++)
      fv.push_back(inPlane(45.f * i - 80.f, 1.f + 0.1f * i));
    const auto fv4 = withW(fv, -1.f);
    anari::setParameterArray1D(
        d, geom, "faceVarying.tangent", fv4.data(), fv4.size());
    pass &= checkFrame(d,
        geom,
        authoredFrames(qProbes, fv, true, -1.f),
        tolerance,
        "'faceVarying.tangent' on quads");
    anari::release(d, geom);
  }

  {
    // one tangent everywhere, under an instance that halves x: tangents
    // transform like directions
    auto geom = flatMesh(d, false);
    const std::vector<float3> tangents(4, float3(1.f, 1.f, 0.f));
    anari::setParameterArray1D(
        d, geom, "vertex.tangent", tangents.data(), tangents.size());
    mat4 xfm = linalg::identity;
    xfm[0][0] = 0.5f;
    const float3 t = linalg::normalize(float3(0.5f, 1.f, 0.f));
    std::vector<Probe> probes;
    for (float2 pos : {float2(-0.2f, -0.1f), float2(-0.35f, 0.2f)})
      probes.push_back({pixelAt(pos), t, linalg::cross(Z, t)});
    pass &= checkFrame(
        d, geom, probes, tolerance, "an instance transforms tangents", xfm);
    anari::release(d, geom);
  }

  return pass;
}

// Warnings ///////////////////////////////////////////////////////////////////

// Whether exactly one warning containing 'expected' came since 'before'
bool warnedOnce(size_t before, const std::string &expected)
{
  const bool once = g_warnings.size() == before + 1
      && g_warnings.back().find(expected) != std::string::npos;
  if (!once) {
    for (size_t i = before; i < g_warnings.size(); i++)
      std::printf("  warning: %s\n", g_warnings[i].c_str());
  }
  return once;
}

bool sameImage(const std::vector<float4> &a, const std::vector<float4> &b)
{
  return std::memcmp(a.data(), b.data(), a.size() * sizeof(float4)) == 0;
}

bool testWarnings(anari::Device d)
{
  bool pass =
      check(g_warnings.empty(), "no warnings before the warning checks");
  auto geom = flatMesh(d, false);
  anari::commitParameters(d, geom);
  auto world = worldOf(d, geom, linalg::identity);
  const auto computed = render(d, world, "quality", "tangent");

  for (const char *name : {"vertex.tangent", "faceVarying.tangent"}) {
    const std::vector<float2> wrong(6, float2(1.f, 0.f));
    const size_t before = g_warnings.size();
    anari::setParameterArray1D(d, geom, name, wrong.data(), wrong.size());
    anari::commitParameters(d, geom);
    const auto image = render(d, world, "quality", "tangent");
    pass &= check(sameImage(image, computed),
        std::string("FLOAT32_VEC2 '") + name + "' is ignored");
    pass &= check(warnedOnce(before,
                      std::string("'") + name
                          + "' array elements are ANARI_FLOAT32_VEC2"),
        std::string("FLOAT32_VEC2 '") + name + "' warns once");
    anari::unsetParameter(d, geom, name);
    anari::commitParameters(d, geom);
  }

  {
    // four tangents for the six corners of two triangles: the last two
    // corners use the fourth
    const size_t before = g_warnings.size();
    anari::setParameterArray1D(
        d, geom, "faceVarying.tangent", TRI_FACE_VARYING_TANGENTS.data(), 4);
    pass &= checkFrame(d,
        geom,
        authoredFrames(triangleProbes(),
            std::vector<float3>(TRI_FACE_VARYING_TANGENTS.begin(),
                TRI_FACE_VARYING_TANGENTS.begin() + 4),
            true,
            1.f),
        1e-4f,
        "a short 'faceVarying.tangent' clamps to its last element");
    pass &= check(warnedOnce(before, "'faceVarying.tangent' has 4 elements"),
        "a short 'faceVarying.tangent' warns once");
  }

  anari::release(d, world);
  anari::release(d, geom);
  return pass;
}

// The 'preview' renderer doesn't read tangents
bool testDefaultRenderer(anari::Device d)
{
  bool pass = true;
  for (bool quads : {false, true}) {
    auto geom = tessellatedSphere(d, quads);
    anari::commitParameters(d, geom);
    auto world = worldOf(d, geom, linalg::identity);
    const auto without = render(d, world, "default", "default");
    const size_t vertices = (SEGMENTS + 1) * (RINGS + 1);
    const std::vector<float4> tangents(vertices, float4(0.f, 1.f, 0.f, -1.f));
    anari::setParameterArray1D(
        d, geom, "vertex.tangent", tangents.data(), tangents.size());
    anari::commitParameters(d, geom);
    const auto with = render(d, world, "default", "default");
    pass &= check(sameImage(without, with),
        std::string("the preview renderer ignores tangents on ")
            + (quads ? "quads" : "triangles"));
    anari::release(d, world);
    anari::release(d, geom);
  }
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
  pass &= testSmoothSphere(d);
  pass &= testAuthored(d);
  pass &= testDefaultRenderer(d);
  pass &= testWarnings(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= check(g_errors.empty(), "no device errors");
  return pass ? 0 : 1;
}
