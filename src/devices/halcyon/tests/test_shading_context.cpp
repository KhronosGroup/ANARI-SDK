// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'quality' renderer's shading context (renderer/pathtracer/
// ShadingContext.isph), through its debug modes, which show one value at the
// pixel center's first visible surface: for every geometry subtype on simple
// shapes, the position and normal, tangent frame, texture coordinates
// ('attribute0'), display colour ('color', grey 0.5 without one), surface
// derivatives (along the texture coordinates where they vary, else the
// geometry's own parameters), normal derivatives, and the screen-space
// derivatives from the camera's ray differential, against their analytic
// values at probe pixels; also through an instance transform and a
// perspective camera.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};
constexpr float PI = 3.14159265358979323846f;
// The camera ray differential's scale, 1 / sqrt(convergedSamplesPerPixel),
// at its default of 256
constexpr float DIFFERENTIAL_SCALE = 1.f / 16.f;
// One pixel of the orthographic camera's [-1, 1]^2 view
constexpr float PIXEL = 2.f / IMAGE_SIZE.x;

// device errors and warnings fail the test
bool g_deviceMessage = false;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING) {
    g_deviceMessage = true;
    std::fprintf(stderr, "[ANARI] %s\n", message);
  }
}

// Expected values /////////////////////////////////////////////////////////////

float3 projectOff(float3 v, float3 n)
{
  return v - n * linalg::dot(n, v);
}

// The shading context's tangent frame around unit normal 'n' from dPdu and
// dPdv where they aren't degenerate: dPdu off the normal, and n x tangent
// signed by the handedness of dPdu and dPdv
void tangentFrame(float3 n, float3 dPdu, float3 dPdv, float3 &t, float3 &b)
{
  t = linalg::normalize(projectOff(dPdu, n));
  const float3 pb = linalg::normalize(projectOff(dPdv, n));
  const float handedness =
      linalg::dot(linalg::cross(t, pb), n) < 0.f ? -1.f : 1.f;
  b = handedness * linalg::cross(n, t);
}

// Where the ray from 'o' along 'dir' meets the plane through 'p' with normal
// 'n', relative to 'p': a screen-space position derivative
float3 planeOffset(float3 o, float3 dir, float3 p, float3 n)
{
  const float t = -linalg::dot(n, o - p) / linalg::dot(n, dir);
  return o + t * dir - p;
}

// The least-squares (du/dx, dv/dx, du/dy, dv/dy) through dPdu and dPdv
float4 texcoordDerivatives(float3 dPdu, float3 dPdv, float3 dPdx, float3 dPdy)
{
  const double a00 = linalg::dot(dPdu, dPdu);
  const double a01 = linalg::dot(dPdu, dPdv);
  const double a11 = linalg::dot(dPdv, dPdv);
  const double det = a00 * a11 - a01 * a01;
  auto solve = [&](float3 d, double &du, double &dv) {
    const double b0 = linalg::dot(dPdu, d);
    const double b1 = linalg::dot(dPdv, d);
    du = (a11 * b0 - a01 * b1) / det;
    dv = (a00 * b1 - a01 * b0) / det;
  };
  double dudx, dvdx, dudy, dvdy;
  solve(dPdx, dudx, dvdx);
  solve(dPdy, dudy, dvdy);
  return float4(float(dudx), float(dvdx), float(dudy), float(dvdy));
}

// The derivative of the unit normal normalize(n) along a parameter whose
// derivative of n is 'dn'
float3 unitNormalDerivative(float3 n, float3 dn)
{
  const float l = linalg::length(n);
  const float3 un = n / l;
  return (dn - un * linalg::dot(un, dn)) / l;
}

// What a shading point should show in each mode
struct Point
{
  float3 position; // object space
  float3 normal;
  float3 tangent;
  float3 bitangent;
  float2 texcoord;
  float4 displayColor;
  float3 dPdu;
  float3 dPdv;
  float3 dndu;
  float3 dndv;
  float3 dPdx;
  float3 dPdy;
  float4 texcoordDerivatives;
};

const std::vector<std::pair<const char *, std::function<float4(const Point &)>>>
    MODES = {
        {"position", [](const Point &p) { return float4(p.position, 1.f); }},
        {"normal", [](const Point &p) { return float4(p.normal, 1.f); }},
        {"tangent", [](const Point &p) { return float4(p.tangent, 1.f); }},
        {"bitangent", [](const Point &p) { return float4(p.bitangent, 1.f); }},
        {"texcoord",
            [](const Point &p) {
              return float4(p.texcoord.x, p.texcoord.y, 0.f, 1.f);
            }},
        {"displayColor", [](const Point &p) { return p.displayColor; }},
        {"dPdu", [](const Point &p) { return float4(p.dPdu, 1.f); }},
        {"dPdv", [](const Point &p) { return float4(p.dPdv, 1.f); }},
        {"dndu", [](const Point &p) { return float4(p.dndu, 1.f); }},
        {"dndv", [](const Point &p) { return float4(p.dndv, 1.f); }},
        {"dPdx", [](const Point &p) { return float4(p.dPdx, 1.f); }},
        {"dPdy", [](const Point &p) { return float4(p.dPdy, 1.f); }},
        {"texcoordDerivatives",
            [](const Point &p) { return p.texcoordDerivatives; }},
};

// The pixel covering (x, y) of the orthographic view and its center's (x, y)
uint2 pixelAt(float2 xy, float2 &center)
{
  const uint2 px(uint32_t((xy.x + 1.f) / 2.f * IMAGE_SIZE.x),
      uint32_t((xy.y + 1.f) / 2.f * IMAGE_SIZE.y));
  center = float2(2.f * (px.x + 0.5f) / IMAGE_SIZE.x - 1.f,
      2.f * (px.y + 0.5f) / IMAGE_SIZE.y - 1.f);
  return px;
}

struct Probe
{
  std::string name;
  uint2 pixel;
  Point expected;
};

// Screen-space derivatives of a hit 'p' with normal 'n' of the orthographic
// camera looking down -z: the rays a scaled pixel over
void orthographicDerivatives(Point &pt, float3 p)
{
  const float3 dir(0.f, 0.f, -1.f);
  const float3 o(p.x, p.y, 2.f);
  pt.dPdx = planeOffset(
      o + float3(PIXEL * DIFFERENTIAL_SCALE, 0.f, 0.f), dir, p, pt.normal);
  pt.dPdy = planeOffset(
      o + float3(0.f, PIXEL * DIFFERENTIAL_SCALE, 0.f), dir, p, pt.normal);
  pt.texcoordDerivatives =
      texcoordDerivatives(pt.dPdu, pt.dPdv, pt.dPdx, pt.dPdy);
}

// Scenes //////////////////////////////////////////////////////////////////////

anari::Surface makeSurface(anari::Device d, anari::Geometry geom)
{
  anari::commitParameters(d, geom);
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

anari::World worldOf(anari::Device d, anari::Surface surface)
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  anari::release(d, surface);
  return world;
}

// A world with 'surface' in one instance with transform 'xfm'
anari::World instancedWorldOf(anari::Device d, anari::Surface surface, mat4 xfm)
{
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

// Orthographic camera looking down -z at the [-1, 1]^2 square
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

struct Scene
{
  const char *name;
  anari::World world;
  anari::Camera camera;
  std::vector<Probe> probes;
};

// A flat right triangle in z = 0 with texture coordinates (0, 0), (2, 0),
// (0, 1) at its corners, so dP/ds = (0.8, 0, 0) and dP/dt = (0, 1.6, 0); no
// 'color', so the display colour is grey
const std::vector<float3> TRIANGLE = {
    {-0.8f, -0.8f, 0.f}, {0.8f, -0.8f, 0.f}, {-0.8f, 0.8f, 0.f}};
const std::vector<float2> TRIANGLE_ST = {{0.f, 0.f}, {2.f, 0.f}, {0.f, 1.f}};

anari::Surface texturedTriangle(anari::Device d)
{
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", TRIANGLE.data(), TRIANGLE.size());
  anari::setParameterArray1D(
      d, geom, "vertex.attribute0", TRIANGLE_ST.data(), TRIANGLE_ST.size());
  return makeSurface(d, geom);
}

// The textured triangle's point at world (x, y), seen through 'xfm' (an x
// scale 'sx' and translation 'tx')
Point texturedTrianglePoint(float2 xy, float sx = 1.f, float tx = 0.f)
{
  const float3 objectP((xy.x - tx) / sx, xy.y, 0.f);
  const float u = (objectP.x + 0.8f) / 1.6f;
  const float v = (objectP.y + 0.8f) / 1.6f;
  Point pt;
  pt.position = objectP;
  pt.normal = float3(0.f, 0.f, 1.f);
  pt.tangent = float3(1.f, 0.f, 0.f);
  pt.bitangent = float3(0.f, 1.f, 0.f);
  pt.texcoord = float2(2.f * u, v);
  pt.displayColor = float4(0.5f, 0.5f, 0.5f, 1.f);
  pt.dPdu = float3(0.8f * sx, 0.f, 0.f);
  pt.dPdv = float3(0.f, 1.6f, 0.f);
  pt.dndu = float3(0.f);
  pt.dndv = float3(0.f);
  // flat: the scaled pixel along x and y, and its texture coordinates
  pt.dPdx = float3(PIXEL * DIFFERENTIAL_SCALE, 0.f, 0.f);
  pt.dPdy = float3(0.f, PIXEL * DIFFERENTIAL_SCALE, 0.f);
  pt.texcoordDerivatives = float4(PIXEL * DIFFERENTIAL_SCALE / pt.dPdu.x,
      0.f,
      0.f,
      PIXEL * DIFFERENTIAL_SCALE / 1.6f);
  return pt;
}

Scene triangleScene(anari::Device d)
{
  Scene s{"triangle", worldOf(d, texturedTriangle(d)), orthographicCamera(d)};
  for (float2 xy :
      {float2(-0.5f, -0.5f), float2(0.1f, -0.6f), float2(-0.6f, 0.3f)}) {
    float2 c;
    const uint2 px = pixelAt(xy, c);
    s.probes.push_back({"triangle", px, texturedTrianglePoint(c)});
  }
  return s;
}

// The textured triangle in an instance that halves x and moves it by 0.2:
// world dP/ds halves, the position is in object space
Scene instanceScene(anari::Device d)
{
  const float sx = 0.5f;
  const float tx = 0.2f;
  mat4 xfm = linalg::identity;
  xfm[0][0] = sx;
  xfm[3][0] = tx;
  Scene s{"instance",
      instancedWorldOf(d, texturedTriangle(d), xfm),
      orthographicCamera(d)};
  for (float2 xy : {float2(-0.1f, -0.5f), float2(0.3f, -0.6f)}) {
    float2 c;
    const uint2 px = pixelAt(xy, c);
    s.probes.push_back({"instance", px, texturedTrianglePoint(c, sx, tx)});
  }
  return s;
}

// The flat triangle without texture coordinates, with vertex normals that
// tilt towards +x at its second corner: dPdu and dPdv are Embree's edges
// (1.6, 0, 0) and (0, 1.6, 0), the normal varies along u only
const float3 TILTED = {0.6f, 0.f, 0.8f};

Scene normalsScene(anari::Device d)
{
  const std::vector<float3> normals = {
      {0.f, 0.f, 1.f}, TILTED, {0.f, 0.f, 1.f}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", TRIANGLE.data(), TRIANGLE.size());
  anari::setParameterArray1D(
      d, geom, "vertex.normal", normals.data(), normals.size());
  Scene s{"vertex normals",
      worldOf(d, makeSurface(d, geom)),
      orthographicCamera(d)};
  for (float2 xy : {float2(-0.5f, -0.5f), float2(0.3f, -0.6f)}) {
    float2 c;
    const uint2 px = pixelAt(xy, c);
    const float u = (c.x + 0.8f) / 1.6f;
    const float v = (c.y + 0.8f) / 1.6f;
    const float3 n =
        (1.f - u - v) * normals[0] + u * normals[1] + v * normals[2];
    Point pt;
    pt.position = float3(c.x, c.y, 0.f);
    pt.normal = linalg::normalize(n);
    pt.dPdu = float3(1.6f, 0.f, 0.f);
    pt.dPdv = float3(0.f, 1.6f, 0.f);
    tangentFrame(pt.normal, pt.dPdu, pt.dPdv, pt.tangent, pt.bitangent);
    pt.texcoord = float2(0.f);
    pt.displayColor = float4(0.5f, 0.5f, 0.5f, 1.f);
    pt.dndu = unitNormalDerivative(n, normals[1] - normals[0]);
    pt.dndv = float3(0.f);
    orthographicDerivatives(pt, pt.position);
    s.probes.push_back({"vertex normals", px, pt});
  }
  return s;
}

// A square quad in z = 0 with face-varying texture coordinates (0, 0),
// (4, 0), (4, 2), (0, 2), so dP/ds = (0.4, 0, 0) and dP/dt = (0, 0.8, 0), and
// a uniform 'color'
const float4 QUAD_COLOR = {0.2f, 0.4f, 0.6f, 0.5f};

Scene quadScene(anari::Device d)
{
  const std::vector<float3> vertices = {{-0.8f, -0.8f, 0.f},
      {0.8f, -0.8f, 0.f},
      {0.8f, 0.8f, 0.f},
      {-0.8f, 0.8f, 0.f}};
  const std::vector<float2> st = {
      {0.f, 0.f}, {4.f, 0.f}, {4.f, 2.f}, {0.f, 2.f}};
  auto geom = anari::newObject<anari::Geometry>(d, "quad");
  anari::setParameterArray1D(
      d, geom, "vertex.position", vertices.data(), vertices.size());
  anari::setParameterArray1D(
      d, geom, "faceVarying.attribute0", st.data(), st.size());
  anari::setParameter(d, geom, "color", QUAD_COLOR);
  Scene s{"quad", worldOf(d, makeSurface(d, geom)), orthographicCamera(d)};
  for (float2 xy : {float2(-0.5f, -0.5f), float2(0.6f, 0.4f)}) {
    float2 c;
    const uint2 px = pixelAt(xy, c);
    Point pt;
    pt.position = float3(c.x, c.y, 0.f);
    pt.normal = float3(0.f, 0.f, 1.f);
    pt.tangent = float3(1.f, 0.f, 0.f);
    pt.bitangent = float3(0.f, 1.f, 0.f);
    pt.texcoord = float2(4.f * (c.x + 0.8f) / 1.6f, 2.f * (c.y + 0.8f) / 1.6f);
    pt.displayColor = QUAD_COLOR;
    pt.dPdu = float3(0.4f, 0.f, 0.f);
    pt.dPdv = float3(0.f, 0.8f, 0.f);
    pt.dndu = float3(0.f);
    pt.dndv = float3(0.f);
    orthographicDerivatives(pt, pt.position);
    s.probes.push_back({"quad", px, pt});
  }
  return s;
}

// A sphere of radius 0.9 centered at (0, 0, -1), seen from above: longitude
// and latitude around +z. The pixel at the view's center sees its +z pole,
// where the frame is the orthonormal one around +z.
Scene sphereScene(anari::Device d)
{
  const float3 center(0.f, 0.f, -1.f);
  const float r = 0.9f;
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
  anari::setParameter(d, geom, "radius", r);
  Scene s{"sphere", worldOf(d, makeSurface(d, geom)), orthographicCamera(d)};
  for (float2 xy :
      {float2(0.5f, 0.3f), float2(-0.4f, -0.6f), float2(0.f, 0.f)}) {
    float2 c;
    const uint2 px = pixelAt(xy, c);
    const float3 p(c.x, c.y, std::sqrt(r * r - c.x * c.x - c.y * c.y));
    const float3 P = center + p;
    const float rho = std::sqrt(p.x * p.x + p.y * p.y);
    Point pt;
    pt.position = P;
    pt.normal = p / r;
    if (rho > 0.f) {
      pt.dPdu = 2.f * PI * float3(-p.y, p.x, 0.f);
      pt.dPdv = PI * float3(-p.z * p.x / rho, -p.z * p.y / rho, rho);
      pt.dndu = pt.dPdu / r;
      pt.dndv = pt.dPdv / r;
    } else {
      // the pole: OpenUSD's orthonormal frame around +z
      pt.dPdu = float3(0.f, -1.f, 0.f);
      pt.dPdv = float3(1.f, 0.f, 0.f);
      pt.dndu = float3(0.f);
      pt.dndv = float3(0.f);
    }
    tangentFrame(pt.normal, pt.dPdu, pt.dPdv, pt.tangent, pt.bitangent);
    pt.texcoord = float2(0.f);
    pt.displayColor = float4(0.5f, 0.5f, 0.5f, 1.f);
    orthographicDerivatives(pt, P);
    s.probes.push_back({rho > 0.f ? "sphere" : "sphere pole", px, pt});
  }
  return s;
}

// A segment along x at y = 0, z = -1 from x = -0.8 to 0.8, as a curve,
// cylinder or cone (radius 0.4 at x = -0.8 to 0.2 at x = 0.8): the curve
// frame, dPdu the axis (1.6, 0, 0) off the normal and dPdv the unit
// N x dPdu, no normal derivatives
Scene segmentScene(anari::Device d, const char *subtype)
{
  const std::vector<float3> vertices = {{-0.8f, 0.f, -1.f}, {0.8f, 0.f, -1.f}};
  const bool cone = std::string(subtype) == "cone";
  const float r0 = cone ? 0.4f : 0.3f;
  const float r1 = cone ? 0.2f : 0.3f;
  auto geom = anari::newObject<anari::Geometry>(d, subtype);
  anari::setParameterArray1D(
      d, geom, "vertex.position", vertices.data(), vertices.size());
  if (cone) {
    const std::vector<float> radii = {r0, r1};
    anari::setParameterArray1D(d, geom, "vertex.radius", radii.data(), 2);
  } else {
    anari::setParameter(d, geom, "radius", r0);
  }
  Scene s{subtype, worldOf(d, makeSurface(d, geom)), orthographicCamera(d)};
  for (float2 xy : {float2(-0.3f, 0.1f), float2(0.4f, -0.12f)}) {
    float2 c;
    const uint2 px = pixelAt(xy, c);
    // the surface above the axis at x: radius r(x), slope dr/dx
    const float slope = (r1 - r0) / 1.6f;
    const float rx = r0 + slope * (c.x + 0.8f);
    const float3 radial(0.f, c.y / rx, std::sqrt(rx * rx - c.y * c.y) / rx);
    const float3 P = float3(c.x, 0.f, -1.f) + rx * radial;
    Point pt;
    pt.position = P;
    pt.normal = linalg::normalize(float3(-slope, 0.f, 0.f) + radial);
    pt.dPdu = projectOff(float3(1.6f, 0.f, 0.f), pt.normal);
    pt.dPdv =
        linalg::normalize(linalg::cross(pt.normal, linalg::normalize(pt.dPdu)));
    pt.dndu = float3(0.f);
    pt.dndv = float3(0.f);
    tangentFrame(pt.normal, pt.dPdu, pt.dPdv, pt.tangent, pt.bitangent);
    pt.texcoord = float2(0.f);
    pt.displayColor = float4(0.5f, 0.5f, 0.5f, 1.f);
    orthographicDerivatives(pt, P);
    s.probes.push_back({subtype, px, pt});
  }
  return s;
}

// A cubic curve of 'basis' (HALCYON_GEOMETRY_CURVE_BASIS) along x at y = 0,
// z = -1, of radius 0.3, over four unevenly spaced control points, so its
// speed varies: the curve frame, dPdu the cubic's derivative x'(u) along x
// (Embree's basis) at the u where x(u) is the probe's x, dPdv the unit
// N x dPdu, no normal derivatives
Scene cubicScene(anari::Device d, const char *basis)
{
  const std::vector<float> xs = {-2.f, -0.9f, 0.6f, 1.8f};
  std::vector<float3> vertices;
  for (float x : xs)
    vertices.push_back({x, 0.f, -1.f});
  const float r = 0.3f;
  auto geom = anari::newObject<anari::Geometry>(d, "curve");
  anari::setParameter(d, geom, "basis", basis);
  anari::setParameterArray1D(
      d, geom, "vertex.position", vertices.data(), vertices.size());
  anari::setParameter(d, geom, "radius", r);

  const bool bspline = std::string(basis) == "bspline";
  // Embree's BSplineBasis and CatmullRomBasis, and their derivatives
  auto weights = [&](float u, bool derivative) {
    const float t = u, s = 1.f - u;
    if (bspline) {
      return derivative
          ? 0.5f * float4(-s * s, -t * t - 4.f * t * s, s * s + 4.f * s * t, t * t)
          : float4(s * s * s,
                4.f * s * s * s + t * t * t + 12.f * s * t * s + 6.f * t * s * t,
                4.f * t * t * t + s * s * s + 12.f * t * s * t + 6.f * s * t * s,
                t * t * t)
              / 6.f;
    }
    return derivative
        ? 0.5f
            * float4(-s * s + 2.f * s * t,
                2.f * t * (3.f * t - 5.f) + 3.f * t * t,
                2.f * s * (3.f * t + 2.f) - 3.f * s * s,
                -2.f * s * t + t * t)
        : 0.5f
            * float4(-t * s * s,
                2.f + t * t * (3.f * t - 5.f),
                2.f + s * s * (3.f * s - 5.f),
                -s * t * t);
  };
  auto along = [&](float u, bool derivative) {
    const float4 w = weights(u, derivative);
    return w.x * xs[0] + w.y * xs[1] + w.z * xs[2] + w.w * xs[3];
  };

  Scene s{bspline ? "curve bspline" : "curve catmullRom",
      worldOf(d, makeSurface(d, geom)),
      orthographicCamera(d)};
  for (float2 xy : {float2(-0.3f, 0.1f), float2(0.4f, -0.12f)}) {
    float2 c;
    const uint2 px = pixelAt(xy, c);
    // x(u) increases: the u of the probe's x by bisection
    float lo = 0.f, hi = 1.f;
    for (int i = 0; i < 60; i++) {
      const float mid = 0.5f * (lo + hi);
      (along(mid, false) < c.x ? lo : hi) = mid;
    }
    const float u = 0.5f * (lo + hi);
    const float3 radial(0.f, c.y / r, std::sqrt(r * r - c.y * c.y) / r);
    const float3 P = float3(c.x, 0.f, -1.f) + r * radial;
    Point pt;
    pt.position = P;
    pt.normal = radial;
    pt.dPdu = projectOff(float3(along(u, true), 0.f, 0.f), pt.normal);
    pt.dPdv =
        linalg::normalize(linalg::cross(pt.normal, linalg::normalize(pt.dPdu)));
    pt.dndu = float3(0.f);
    pt.dndv = float3(0.f);
    tangentFrame(pt.normal, pt.dPdu, pt.dPdv, pt.tangent, pt.bitangent);
    pt.texcoord = float2(0.f);
    pt.displayColor = float4(0.5f, 0.5f, 0.5f, 1.f);
    orthographicDerivatives(pt, P);
    s.probes.push_back({s.name, px, pt});
  }
  return s;
}

// The textured triangle seen by a perspective camera at (0, 0, 2) looking
// down -z (fovy 60 degrees): the differential rays go through the next
// pixels' centers, brought towards the ray by the scale
Scene perspectiveScene(anari::Device d)
{
  const float fovy = PI / 3.f;
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "fovy", fovy);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  Scene s{"perspective", worldOf(d, texturedTriangle(d)), camera};

  // the ray through screen point (sx, sy) in [0, 1]^2
  const float h = 2.f * std::tan(0.5f * fovy);
  auto rayDir = [&](float sx, float sy) {
    return linalg::normalize(
        float3(0.f, 0.f, -1.f) + float3((sx - 0.5f) * h, (sy - 0.5f) * h, 0.f));
  };
  const float3 o(0.f, 0.f, 2.f);
  const float3 n(0.f, 0.f, 1.f);
  for (uint2 px : {uint2(12, 10), uint2(15, 20)}) {
    const float sx = (px.x + 0.5f) / IMAGE_SIZE.x;
    const float sy = (px.y + 0.5f) / IMAGE_SIZE.y;
    const float3 dir = rayDir(sx, sy);
    const float3 P = o + (2.f / -dir.z) * dir;
    Point pt = texturedTrianglePoint(float2(P.x, P.y));
    auto scaled = [&](float3 next) {
      return linalg::normalize(dir + (next - dir) * DIFFERENTIAL_SCALE);
    };
    pt.dPdx = planeOffset(o, scaled(rayDir(sx + 1.f / IMAGE_SIZE.x, sy)), P, n);
    pt.dPdy = planeOffset(o, scaled(rayDir(sx, sy + 1.f / IMAGE_SIZE.y)), P, n);
    pt.texcoordDerivatives =
        texcoordDerivatives(pt.dPdu, pt.dPdv, pt.dPdx, pt.dPdy);
    s.probes.push_back({"perspective", px, pt});
  }
  return s;
}

// Rendering ///////////////////////////////////////////////////////////////////

bool near(float4 a, float4 b)
{
  for (int i = 0; i < 4; i++) {
    if (!(std::abs(a[i] - b[i]) <= 1e-4f + 1e-4f * std::abs(b[i])))
      return false;
  }
  return true;
}

bool testScene(anari::Device d, const Scene &scene)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "camera", scene.camera);
  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", scene.world);
  anari::commitParameters(d, frame);

  bool pass = true;
  for (const auto &[mode, value] : MODES) {
    anari::setParameter(d, renderer, "mode", mode);
    anari::commitParameters(d, renderer);
    anari::render(d, frame);
    anari::wait(d, frame);

    bool modePass = true;
    auto fb = anari::map<float4>(d, frame, "channel.color");
    for (const auto &p : scene.probes) {
      const float4 expected = value(p.expected);
      const float4 got = fb.data[size_t(p.pixel.y) * IMAGE_SIZE.x + p.pixel.x];
      if (!near(got, expected)) {
        std::printf(
            "FAIL %s %s, %s at (%u, %u): got (%g, %g, %g, %g), expected "
            "(%g, %g, %g, %g)\n",
            scene.name,
            mode,
            p.name.c_str(),
            p.pixel.x,
            p.pixel.y,
            got.x,
            got.y,
            got.z,
            got.w,
            expected.x,
            expected.y,
            expected.z,
            expected.w);
        modePass = false;
      }
    }
    // a pixel that misses shows transparent black
    const float4 corner = fb.data[0];
    if (std::string(scene.name) != "perspective"
        && !near(corner, float4(0.f))) {
      std::printf(
          "FAIL %s %s: the corner pixel doesn't miss\n", scene.name, mode);
      modePass = false;
    }
    anari::unmap(d, frame, "channel.color");
    pass &= modePass;
  }

  anari::release(d, frame);
  anari::release(d, renderer);
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", scene.name);
  return pass;
}

// The renderer's 'convergedSamplesPerPixel' sets the differentials' scale,
// 1 / sqrt of it (Typhoon's samplesToConvergence): dPdx on the flat triangle
// under the orthographic camera is proportional to it
bool testDifferentialScale(anari::Device d, const Scene &scene)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  anari::setParameter(d, renderer, "mode", "dPdx");
  anari::commitParameters(d, renderer);
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "camera", scene.camera);
  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", scene.world);
  anari::commitParameters(d, frame);

  const uint2 px = scene.probes.front().pixel;
  auto dPdx = [&](int samples) {
    anari::setParameter(d, renderer, "convergedSamplesPerPixel", samples);
    anari::commitParameters(d, renderer);
    anari::render(d, frame);
    anari::wait(d, frame);
    auto fb = anari::map<float4>(d, frame, "channel.color");
    const float4 v = fb.data[size_t(px.y) * IMAGE_SIZE.x + px.x];
    anari::unmap(d, frame, "channel.color");
    return v;
  };

  const float4 at256 = dPdx(256);
  const float4 at64 = dPdx(64);
  const float4 at1 = dPdx(1);
  const float4 at0 = dPdx(0);
  anari::release(d, frame);
  anari::release(d, renderer);

  auto scaled = [](float4 v, float s) {
    return float4(v.x * s, v.y * s, v.z * s, v.w);
  };
  const bool pass = std::abs(at256.x) > 0.f
      && near(at256, float4(scene.probes.front().expected.dPdx, 1.f))
      && near(at64, scaled(at256, 2.f))
      && near(at1, scaled(at256, 16.f))
      // clamped to 1
      && near(at0, at1);
  std::printf("%s convergedSamplesPerPixel scales the differentials\n",
      pass ? "PASS" : "FAIL");
  return pass;
}

// Object position from the mesh ///////////////////////////////////////////////

// A MaterialX document reading the object position: emission of the
// cellnoise of it (a grey of the integer cell), on a black surface
const char *const CELLNOISE_DOCUMENT = R"(<?xml version="1.0"?>
<materialx version="1.39">
  <position name="P" type="vector3" space="object" />
  <cellnoise3d name="noise" type="float">
    <input name="position" type="vector3" nodename="P" />
  </cellnoise3d>
  <convert name="grey" type="color3">
    <input name="in" type="float" nodename="noise" />
  </convert>
  <standard_surface name="S" type="surfaceshader">
    <input name="base" type="float" value="0" />
    <input name="specular" type="float" value="0" />
    <input name="emission" type="float" value="1" />
    <input name="emission_color" type="color3" nodename="grey" />
  </standard_surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
  </surfacematerial>
</materialx>)";

// The plane y = 0 of object space, over [0.05, 0.95] in x and z (one cell of
// the noise), as two triangles or one quad that isn't a parallelogram
const std::vector<float3> PLANE = {{0.05f, 0.f, 0.05f},
    {0.95f, 0.f, 0.1f},
    {0.9f, 0.f, 0.95f},
    {0.1f, 0.f, 0.9f}};

anari::Geometry planeGeometry(anari::Device d, const char *subtype)
{
  auto geom = anari::newObject<anari::Geometry>(d, subtype);
  if (std::string(subtype) == "sphere") {
    const float3 center(0.5f, 0.f, 0.5f);
    anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
    anari::setParameter(d, geom, "radius", 0.4f);
  } else {
    anari::setParameterArray1D(
        d, geom, "vertex.position", PLANE.data(), PLANE.size());
    if (std::string(subtype) == "triangle") {
      const std::vector<uint3> index = {{0, 1, 2}, {0, 2, 3}};
      anari::setParameterArray1D(
          d, geom, "primitive.index", index.data(), index.size());
    }
  }
  anari::commitParameters(d, geom);
  return geom;
}

// An instance turning the plane almost to face the camera (+z), about x and
// z, so that the hit taken back to object space rounds off the plane
mat4 tiltedPlaneTransform()
{
  const float a = 0.5f * PI - 0.25f;
  const float b = 0.4f;
  mat4 xfm = linalg::identity;
  xfm[0] = float4(std::cos(b), std::sin(b), 0.f, 0.f);
  xfm[1] = float4(-std::sin(b) * std::cos(a),
      std::cos(b) * std::cos(a),
      std::sin(a),
      0.f);
  xfm[2] = float4(std::sin(b) * std::sin(a),
      -std::cos(b) * std::sin(a),
      std::cos(a),
      0.f);
  xfm[3] = float4(-0.45f, 0.35f, 0.1f, 1.f);
  return xfm;
}

// The colour channel of one render of 'material' on the tilted plane of
// 'subtype' in renderer 'mode'
std::vector<float4> renderTiltedPlane(anari::Device d,
    const char *subtype,
    anari::Material material,
    const char *mode)
{
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(
      d, surface, "geometry", planeGeometry(d, subtype));
  anari::setParameter(d, surface, "material", material);
  anari::commitParameters(d, surface);
  auto world = instancedWorldOf(d, surface, tiltedPlaneTransform());
  auto camera = orthographicCamera(d);
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  anari::setParameter(d, renderer, "mode", mode);
  anari::setParameter(d, renderer, "maxBounces", 0);
  anari::setParameter(d, renderer, "background", float4(0.f));
  anari::commitParameters(d, renderer);
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);
  auto fb = anari::map<float4>(d, frame, "channel.color");
  std::vector<float4> pixels(fb.data, fb.data + IMAGE_SIZE.x * IMAGE_SIZE.y);
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);
  return pixels;
}

// Graphs that read the object position get the surface's own, from the
// primitive's vertices (Typhoon's ResolveObjectSpaceSurfacePosition()):
// through a turned instance, the 'position' mode shows y exactly 0 on the
// plane y = 0 of triangles and quads, where a matte material (whose graph
// doesn't read it) shows the hit taken back to object space, a few ulps off
// it; so the cellnoise of the position takes one cell over the whole plane,
// as in Typhoon (procedural3d_gallery's planes). Spheres keep the hit.
bool testObjectPosition(anari::Device d)
{
  auto matte = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, matte);
  auto cellnoise = anari::newObject<anari::Material>(d, "materialx");
  anari::setParameter(d, cellnoise, "sourceType", "documentInline");
  anari::setParameter(d, cellnoise, "source", CELLNOISE_DOCUMENT);
  anari::commitParameters(d, cellnoise);

  bool pass = true;
  for (const char *subtype : {"triangle", "quad"}) {
    const auto hit = renderTiltedPlane(d, subtype, matte, "position");
    const auto mesh = renderTiltedPlane(d, subtype, cellnoise, "position");
    const auto colour = renderTiltedPlane(d, subtype, cellnoise, "default");
    int hits = 0;
    int offPlane = 0;
    bool onPlane = true;
    bool near = true;
    bool oneCell = true;
    float4 cell(-1.f);
    for (size_t i = 0; i < hit.size(); i++) {
      if (hit[i].w != 1.f || mesh[i].w != 1.f)
        continue;
      hits++;
      offPlane += hit[i].y != 0.f;
      onPlane &= mesh[i].y == 0.f;
      near &= std::abs(mesh[i].x - hit[i].x) <= 1e-5f
          && std::abs(mesh[i].z - hit[i].z) <= 1e-5f;
      if (colour[i].w == 1.f) {
        if (cell.w < 0.f)
          cell = colour[i];
        oneCell &= colour[i] == cell;
      }
    }
    const bool ok =
        hits > 100 && offPlane > 0 && onPlane && near && oneCell && cell.w > 0.f;
    std::printf(
        "%s %s: the object position from the mesh is on the plane (%d of %d "
        "hits off it, %s), one cell of cellnoise %s\n",
        ok ? "PASS" : "FAIL",
        subtype,
        offPlane,
        hits,
        onPlane ? "none from the mesh" : "SOME FROM THE MESH",
        oneCell ? "everywhere" : "NOT EVERYWHERE");
    pass &= ok;
  }

  // spheres keep the hit's position: the same bits for both materials
  const auto hit = renderTiltedPlane(d, "sphere", matte, "position");
  const auto mesh = renderTiltedPlane(d, "sphere", cellnoise, "position");
  int sphereHits = 0;
  for (const float4 &p : hit)
    sphereHits += p.w == 1.f;
  const bool same = sphereHits > 100 && hit == mesh;
  std::printf(
      "%s sphere: the object position stays the hit's\n", same ? "PASS" : "FAIL");
  pass &= same;

  anari::release(d, matte);
  anari::release(d, cellnoise);
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

  std::vector<Scene> scenes;
  scenes.push_back(triangleScene(d));
  scenes.push_back(instanceScene(d));
  scenes.push_back(normalsScene(d));
  scenes.push_back(quadScene(d));
  scenes.push_back(sphereScene(d));
  scenes.push_back(segmentScene(d, "curve"));
  scenes.push_back(cubicScene(d, "bspline"));
  scenes.push_back(cubicScene(d, "catmullRom"));
  scenes.push_back(segmentScene(d, "cylinder"));
  scenes.push_back(segmentScene(d, "cone"));
  scenes.push_back(perspectiveScene(d));

  bool pass = testDifferentialScale(d, scenes.front());
  pass &= testObjectPosition(d);
  for (const auto &scene : scenes) {
    pass &= testScene(d, scene);
    anari::release(d, scene.world);
    anari::release(d, scene.camera);
  }

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceMessage)
    std::printf("FAIL: device reported warnings or errors\n");

  return pass && !g_deviceMessage ? 0 : 1;
}
