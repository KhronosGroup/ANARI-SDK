// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "WorldBuilder.h"
#include "generators/ColorPalette.h"
#include "generators/TextureGenerator.h"
// std
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace anari {
namespace cts {

namespace scenes = anari::scenes;

// --- Generic axis-value parameter binding ------------------------------------

namespace detail {

void setBoundParameterImpl(anari::Device d,
    anari::Object obj,
    const char *param,
    const Any &value,
    const std::vector<anari::Sampler> &samplers)
{
  if (!value.valid())
    return; // none() -> leave unset so the device default applies

  const ANARIDataType t = value.type();

  if (t == ANARI_STRING) {
    const std::string s = value.getString();
    const std::string refPrefix = "ref_sampler_";
    if (s.rfind(refPrefix, 0) == 0) {
      size_t idx = 0;
      try {
        idx = static_cast<size_t>(std::stoul(s.substr(refPrefix.size())));
      } catch (const std::exception &) {
        return;
      }
      if (idx < samplers.size()) {
        anari::Sampler sm = samplers[idx];
        anariSetParameter(d, obj, param, ANARI_SAMPLER, &sm);
      }
      return;
    }
    // A plain string is an attribute name (e.g. "color", "attribute0").
    anariSetParameter(d, obj, param, ANARI_STRING, s.c_str());
    return;
  }

  if (anari::isObject(t))
    return; // object handles are not carried as axis values

  // Scalars, vectors, matrices, boxes: set the parameter with its exact stored
  // type and raw bytes.
  anariSetParameter(d, obj, param, t, value.data());
}

} // namespace detail

// --- Geometry: deterministic Layout (ADR-0007) -------------------------------

namespace {

using vec2u = math::vec<uint32_t, 2>;
using vec3u = math::vec<uint32_t, 3>;
using vec4u = math::vec<uint32_t, 4>;

// Grid + primitive proportions, all in world units. The grid is centered at the
// origin in the XY plane; primitives face +Z (toward the bounds-framed camera).
constexpr float kPitch = 0.5f; // cell-to-cell spacing
constexpr float kGutter =
    0.2f; // fraction of a cell left empty around a primitive
constexpr float kPrimSize = kPitch * (1.f - kGutter); // primitive extent (0.4)
constexpr float kHalf = kPrimSize * 0.5f; // half extent (0.2)
constexpr float kTubeRadius =
    0.3f * kPitch; // cone base / cylinder radius (0.15)
constexpr float kSphereRadius = 0.3f * kPitch; // sphere radius (0.15)
constexpr float kCurveRadius = 0.1f * kPitch; // curve segment radius (0.05)
constexpr float kCubeYawDeg = 35.f; // cube three-quarter view yaw (about Y)
constexpr float kCubePitchDeg = 25.f; // cube three-quarter view pitch (about X)

// Columns/rows of the near-square grid holding `n` primitives.
struct Grid
{
  int cols;
  int rows;
};

Grid gridFor(int n)
{
  const int count = n < 1 ? 1 : n;
  const int cols =
      static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
  const int rows = (count + cols - 1) / cols;
  return {cols, rows};
}

// World-space center of the cell holding primitive `i`, grid centered at
// origin. Primitive 0 is the top-left cell; index increases left-to-right,
// top-to-bottom.
math::float3 cellCenter(int i, const Grid &g)
{
  const int r = i / g.cols;
  const int c = i % g.cols;
  const float x = (static_cast<float>(c) - (g.cols - 1) * 0.5f) * kPitch;
  const float y = ((g.rows - 1) * 0.5f - static_cast<float>(r)) * kPitch;
  return math::float3(x, y, 0.f);
}

// The eight corners of a unit cube tilted into a three-quarter view, in the
// canonical 0..7 vertex order, centered at `c`. Index order matches the cube
// index tables below.
std::array<math::float3, 8> cubeCorners(const math::float3 &c)
{
  // yaw about Y, then pitch about X — computed once.
  static const math::mat4 rot = math::mul(
      math::rotation_matrix(math::rotation_quat(
          math::float3(1.f, 0.f, 0.f), anari::radians(kCubePitchDeg))),
      math::rotation_matrix(math::rotation_quat(
          math::float3(0.f, 1.f, 0.f), anari::radians(kCubeYawDeg))));
  // Canonical 0..1 corner coordinates (matching PrimitiveGenerator's indexed
  // cube), remapped to a [-kHalf, kHalf] cube and rotated.
  static const std::array<math::float3, 8> unit = {math::float3(0, 0, 0),
      math::float3(1, 0, 0),
      math::float3(0, 1, 0),
      math::float3(0, 0, 1),
      math::float3(1, 1, 0),
      math::float3(1, 0, 1),
      math::float3(0, 1, 1),
      math::float3(1, 1, 1)};
  std::array<math::float3, 8> out;
  for (size_t i = 0; i < 8; ++i) {
    const math::float3 local((unit[i].x * 2.f - 1.f) * kHalf,
        (unit[i].y * 2.f - 1.f) * kHalf,
        (unit[i].z * 2.f - 1.f) * kHalf);
    out[i] = c + math::mul(rot, math::float4(local, 1.f)).xyz();
  }
  return out;
}

// Triangle face indices of the canonical cube (12 triangles).
const std::array<vec3u, 12> kCubeTris = {vec3u{0, 2, 1},
    vec3u{1, 2, 4},
    vec3u{1, 4, 5},
    vec3u{5, 4, 7},
    vec3u{5, 7, 3},
    vec3u{6, 7, 3},
    vec3u{0, 3, 6},
    vec3u{0, 6, 2},
    vec3u{2, 7, 4},
    vec3u{2, 6, 7},
    vec3u{0, 5, 1},
    vec3u{0, 3, 5}};

// Quad face indices of the canonical cube (6 quads).
const std::array<vec4u, 6> kCubeQuads = {vec4u{0, 2, 4, 1},
    vec4u{1, 4, 7, 5},
    vec4u{5, 7, 6, 3},
    vec4u{0, 3, 6, 2},
    vec4u{2, 6, 7, 4},
    vec4u{0, 1, 5, 3}};

// The four corners of a unit square facing +Z, centered at `c`, CCW from +Z.
std::array<math::float3, 4> squareCorners(const math::float3 &c)
{
  return {c + math::float3(-kHalf, -kHalf, 0.f),
      c + math::float3(kHalf, -kHalf, 0.f),
      c + math::float3(kHalf, kHalf, 0.f),
      c + math::float3(-kHalf, kHalf, 0.f)};
}

// soup: two triangles per square, expanded as 6 vertices.
std::vector<math::float3> layoutTriQuadsSoup(int n)
{
  const Grid g = gridFor(n);
  std::vector<math::float3> v;
  v.reserve(static_cast<size_t>(n) * 6);
  for (int i = 0; i < n; ++i) {
    const auto s = squareCorners(cellCenter(i, g));
    v.insert(v.end(), {s[0], s[1], s[2], s[0], s[2], s[3]});
  }
  return v;
}

// indexed: four shared vertices per square + two-triangle index list.
std::pair<std::vector<math::float3>, std::vector<vec3u>> layoutTriQuadsIndexed(
    int n)
{
  const Grid g = gridFor(n);
  std::vector<math::float3> v;
  std::vector<vec3u> idx;
  v.reserve(static_cast<size_t>(n) * 4);
  idx.reserve(static_cast<size_t>(n) * 2);
  for (int i = 0; i < n; ++i) {
    const auto s = squareCorners(cellCenter(i, g));
    const uint32_t b = static_cast<uint32_t>(i) * 4u;
    v.insert(v.end(), {s[0], s[1], s[2], s[3]});
    idx.push_back(vec3u{b, b + 1u, b + 2u});
    idx.push_back(vec3u{b, b + 2u, b + 3u});
  }
  return {v, idx};
}

// soup: 36 vertices per cube, expanded from the triangle index table.
std::vector<math::float3> layoutTriCubesSoup(int n)
{
  const Grid g = gridFor(n);
  std::vector<math::float3> v;
  v.reserve(static_cast<size_t>(n) * 36);
  for (int i = 0; i < n; ++i) {
    const auto corners = cubeCorners(cellCenter(i, g));
    for (const auto &t : kCubeTris)
      v.insert(v.end(), {corners[t.x], corners[t.y], corners[t.z]});
  }
  return v;
}

// indexed: eight shared corners per cube + the triangle index table.
std::pair<std::vector<math::float3>, std::vector<vec3u>> layoutTriCubesIndexed(
    int n)
{
  const Grid g = gridFor(n);
  std::vector<math::float3> v;
  std::vector<vec3u> idx;
  v.reserve(static_cast<size_t>(n) * 8);
  idx.reserve(static_cast<size_t>(n) * 12);
  for (int i = 0; i < n; ++i) {
    const auto corners = cubeCorners(cellCenter(i, g));
    const uint32_t b = static_cast<uint32_t>(i) * 8u;
    v.insert(v.end(), corners.begin(), corners.end());
    for (const auto &t : kCubeTris)
      idx.push_back(vec3u{t.x + b, t.y + b, t.z + b});
  }
  return {v, idx};
}

// soup: four vertices per quad.
std::vector<math::float3> layoutQuads(int n)
{
  const Grid g = gridFor(n);
  std::vector<math::float3> v;
  v.reserve(static_cast<size_t>(n) * 4);
  for (int i = 0; i < n; ++i) {
    const auto s = squareCorners(cellCenter(i, g));
    v.insert(v.end(), s.begin(), s.end());
  }
  return v;
}

// soup: 24 vertices per cube, expanded from the quad index table.
std::vector<math::float3> layoutQuadCubesSoup(int n)
{
  const Grid g = gridFor(n);
  std::vector<math::float3> v;
  v.reserve(static_cast<size_t>(n) * 24);
  for (int i = 0; i < n; ++i) {
    const auto corners = cubeCorners(cellCenter(i, g));
    for (const auto &q : kCubeQuads)
      v.insert(
          v.end(), {corners[q.x], corners[q.y], corners[q.z], corners[q.w]});
  }
  return v;
}

// indexed: eight shared corners per cube + the quad index table.
std::pair<std::vector<math::float3>, std::vector<vec4u>> layoutQuadCubesIndexed(
    int n)
{
  const Grid g = gridFor(n);
  std::vector<math::float3> v;
  std::vector<vec4u> idx;
  v.reserve(static_cast<size_t>(n) * 8);
  idx.reserve(static_cast<size_t>(n) * 6);
  for (int i = 0; i < n; ++i) {
    const auto corners = cubeCorners(cellCenter(i, g));
    const uint32_t b = static_cast<uint32_t>(i) * 8u;
    v.insert(v.end(), corners.begin(), corners.end());
    for (const auto &q : kCubeQuads)
      idx.push_back(vec4u{q.x + b, q.y + b, q.z + b, q.w + b});
  }
  return {v, idx};
}

// One sphere center per cell.
std::vector<math::float3> layoutSpheres(int n)
{
  const Grid g = gridFor(n);
  std::vector<math::float3> v;
  v.reserve(n);
  for (int i = 0; i < n; ++i)
    v.push_back(cellCenter(i, g));
  return v;
}

// Two vertices per primitive: a vertical segment from the cell's bottom to its
// top. Used by cones and cylinders, which stand upright facing +Z with a fixed
// bottom->top orientation (cone base at the bottom, apex at the top).
std::vector<math::float3> layoutVerticalSegments(int n)
{
  const Grid g = gridFor(n);
  std::vector<math::float3> v;
  v.reserve(static_cast<size_t>(n) * 2);
  for (int i = 0; i < n; ++i) {
    const math::float3 c = cellCenter(i, g);
    v.push_back(c + math::float3(0.f, -kHalf, 0.f)); // bottom
    v.push_back(c + math::float3(0.f, kHalf, 0.f)); // top
  }
  return v;
}

// Vertical curve segments, but ordered so the un-indexed (soup) polyline —
// which connects all consecutive vertices — reads as a clean crenellated wave
// rather than a diagonal scribble: walk the grid in a boustrophedon (reverse
// every other row) and alternate each segment's bottom->top / top->bottom
// direction, so segment-to-segment connectors run along the cell tops/bottoms.
// The indexed variant still selects the upright vertical bars.
std::vector<math::float3> layoutCurveSegments(int n)
{
  const Grid g = gridFor(n);
  std::vector<math::float3> v;
  v.reserve(static_cast<size_t>(n) * 2);
  for (int i = 0; i < n; ++i) {
    const int r = i / g.cols;
    int c = i % g.cols;
    if (r % 2 == 1)
      c = g.cols - 1 - c; // reverse odd rows
    const math::float3 ctr = cellCenter(r * g.cols + c, g);
    const math::float3 lo = ctr + math::float3(0.f, -kHalf, 0.f);
    const math::float3 hi = ctr + math::float3(0.f, kHalf, 0.f);
    if (i % 2 == 0) {
      v.push_back(lo);
      v.push_back(hi);
    } else {
      v.push_back(hi);
      v.push_back(lo);
    }
  }
  return v;
}

// Deterministic 0..1 ramp across n elements.
float ramp01(size_t i, size_t n)
{
  return n > 1 ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.f;
}

// Fill prefix.attribute0..3 with deterministic ramps in [lo, hi]. The values
// are invisible on matte; this only keeps the array paths exercised (ADR-0007).
void setAttributeRamps(anari::Device d,
    anari::Geometry geom,
    const char *prefix,
    size_t n,
    float lo,
    float hi)
{
  std::vector<float> a0(n);
  std::vector<math::float2> a1(n);
  std::vector<math::float3> a2(n);
  std::vector<math::float4> a3(n);
  for (size_t i = 0; i < n; ++i) {
    const float t = lo + (hi - lo) * ramp01(i, n);
    a0[i] = t;
    a1[i] = math::float2(t);
    a2[i] = math::float3(t);
    a3[i] = math::float4(t);
  }
  const std::string p = prefix;
  anari::setAndReleaseParameter(d,
      geom,
      (p + ".attribute0").c_str(),
      anari::newArray1D(d, a0.data(), a0.size()));
  anari::setAndReleaseParameter(d,
      geom,
      (p + ".attribute1").c_str(),
      anari::newArray1D(d, a1.data(), a1.size()));
  anari::setAndReleaseParameter(d,
      geom,
      (p + ".attribute2").c_str(),
      anari::newArray1D(d, a2.data(), a2.size()));
  anari::setAndReleaseParameter(d,
      geom,
      (p + ".attribute3").c_str(),
      anari::newArray1D(d, a3.data(), a3.size()));
}

} // namespace

std::vector<math::float3> layoutTriangleSoup(int count)
{
  const Grid g = gridFor(count);
  std::vector<math::float3> v;
  v.reserve(static_cast<size_t>(count) * 3);
  for (int i = 0; i < count; ++i) {
    const math::float3 c = cellCenter(i, g);
    // Equilateral triangle inscribed in a radius-kHalf circle, apex up, CCW
    // from +Z so its face normal points at the camera.
    constexpr float kSqrt3Over2 = 0.8660254f;
    v.push_back(c + math::float3(0.f, kHalf, 0.f));
    v.push_back(c + math::float3(-kHalf * kSqrt3Over2, -kHalf * 0.5f, 0.f));
    v.push_back(c + math::float3(kHalf * kSqrt3Over2, -kHalf * 0.5f, 0.f));
  }
  return v;
}

anari::Geometry buildGeometry(anari::Device d, const GeometryOptions &opts)
{
  auto geom = anari::newObject<anari::Geometry>(d, opts.subtype.c_str());

  const std::string &subtype = opts.subtype;
  const std::string &shape = opts.shape;
  const int primitiveCount = opts.primitiveCount;
  const bool indexed = opts.primitiveMode == "indexed";

  size_t componentCount = 3;
  if (subtype == "quad")
    componentCount = 4;
  else if (subtype == "sphere" || subtype == "curve")
    componentCount = 1;
  else if (subtype == "cone" || subtype == "cylinder")
    componentCount = 2;

  size_t indiciCount = 0;
  std::vector<math::float3> vertices;

  if (subtype == "triangle") {
    std::vector<vec3u> indices;
    if (shape == "triangle") {
      vertices = layoutTriangleSoup(primitiveCount);
      if (indexed) {
        for (size_t i = 0; i < vertices.size(); i += 3) {
          const auto idx = static_cast<uint32_t>(i);
          indices.push_back(vec3u(idx, idx + 1u, idx + 2u));
        }
      }
    } else if (shape == "quad") {
      if (indexed) {
        auto [v, i] = layoutTriQuadsIndexed(primitiveCount);
        vertices = v;
        indices = i;
      } else {
        vertices = layoutTriQuadsSoup(primitiveCount);
      }
    } else if (shape == "cube") {
      if (indexed) {
        auto [v, i] = layoutTriCubesIndexed(primitiveCount);
        vertices = v;
        indices = i;
      } else {
        vertices = layoutTriCubesSoup(primitiveCount);
      }
    }
    if (indexed) {
      if (opts.unusedVertices && !indices.empty())
        indices.resize(indices.size() - 1);
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (subtype == "quad") {
    std::vector<vec4u> indices;
    if (shape == "quad") {
      vertices = layoutQuads(primitiveCount);
      if (indexed) {
        for (size_t i = 0; i < vertices.size(); i += 4) {
          const auto idx = static_cast<uint32_t>(i);
          indices.push_back(vec4u(idx, idx + 1u, idx + 2u, idx + 3u));
        }
      }
    } else if (shape == "cube") {
      if (indexed) {
        auto [v, i] = layoutQuadCubesIndexed(primitiveCount);
        vertices = v;
        indices = i;
      } else {
        vertices = layoutQuadCubesSoup(primitiveCount);
      }
    }
    if (indexed) {
      if (opts.unusedVertices && !indices.empty())
        indices.resize(indices.size() - 1);
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (subtype == "sphere") {
    vertices = layoutSpheres(primitiveCount);
    if (opts.globalRadius.has_value()) {
      anari::setParameter(d, geom, "radius", opts.globalRadius.value());
    } else {
      std::vector<float> radii(vertices.size(), kSphereRadius);
      anari::setAndReleaseParameter(
          d, geom, "vertex.radius", anari::newArray1D(d, radii.data(), radii.size()));
    }
    if (indexed) {
      std::vector<uint32_t> indices;
      for (size_t i = 0; i < vertices.size(); ++i)
        indices.push_back(static_cast<uint32_t>(i));
      if (opts.unusedVertices && !indices.empty())
        indices.resize(indices.size() - 1);
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (subtype == "curve") {
    vertices = layoutCurveSegments(primitiveCount);
    if (opts.globalRadius.has_value()) {
      anari::setParameter(d, geom, "radius", opts.globalRadius.value());
    } else {
      std::vector<float> radii(vertices.size(), kCurveRadius);
      anari::setAndReleaseParameter(
          d, geom, "vertex.radius", anari::newArray1D(d, radii.data(), radii.size()));
    }
    if (indexed) {
      std::vector<uint32_t> indices;
      for (uint32_t i = 0; i < vertices.size() / 2; i++)
        indices.push_back(i * 2);
      if (opts.unusedVertices && !indices.empty())
        indices.resize(indices.size() - 1);
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (subtype == "cone" || subtype == "cylinder") {
    const bool cone = subtype == "cone";
    vertices = layoutVerticalSegments(primitiveCount);
    std::vector<uint8_t> caps;
    if (opts.vertexCaps.has_value()) {
      const uint8_t cap = opts.vertexCaps.value() == 0 ? 0u : 1u;
      caps.assign(vertices.size(), cap);
    }
    if (cone) {
      // True cone: base radius at the bottom vertex, apex radius 0 at the top.
      std::vector<float> radii;
      radii.reserve(vertices.size());
      for (int i = 0; i < primitiveCount; ++i) {
        radii.push_back(kTubeRadius);
        radii.push_back(0.f);
      }
      anari::setAndReleaseParameter(d,
          geom,
          "vertex.radius",
          anari::newArray1D(d, radii.data(), radii.size()));
    } else if (opts.globalRadius.has_value()) {
      anari::setParameter(d, geom, "radius", opts.globalRadius.value());
    } else {
      std::vector<float> radii(primitiveCount, kTubeRadius); // one per cylinder
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.radius",
          anari::newArray1D(d, radii.data(), radii.size()));
    }
    if (!caps.empty()) {
      anari::setAndReleaseParameter(
          d, geom, "vertex.cap", anari::newArray1D(d, caps.data(), caps.size()));
    }
    anari::setParameter(d, geom, "caps", opts.globalCaps);
    if (indexed) {
      std::vector<vec2u> indices;
      for (uint32_t i = 0; i < vertices.size(); i += 2)
        indices.emplace_back(i, i + 1);
      if (opts.unusedVertices && !indices.empty())
        indices.resize(indices.size() - 1);
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  }

  if (!opts.colorAttribute.empty()) {
    size_t colorCount = vertices.size();
    if (opts.colorAttribute.rfind("primitive", 0) == 0)
      colorCount = primitiveCount;
    auto attributeColor = scenes::colors::getColorVectorFromPalette(colorCount);
    anari::setAndReleaseParameter(d,
        geom,
        opts.colorAttribute.c_str(),
        anari::newArray1D(d, attributeColor.data(), attributeColor.size()));
  }

  if (!opts.opacityAttribute.empty()) {
    size_t opacityCount = vertices.size();
    if (opts.opacityAttribute.rfind("primitive", 0) == 0)
      opacityCount = primitiveCount;
    std::vector<float> attributeOpacity(opacityCount);
    for (size_t i = 0; i < opacityCount; ++i)
      attributeOpacity[i] = ramp01(i, opacityCount);
    anari::setAndReleaseParameter(d,
        geom,
        opts.opacityAttribute.c_str(),
        anari::newArray1D(d, attributeOpacity.data(), attributeOpacity.size()));
  }

  if (!vertices.empty()) {
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.position",
        anari::newArray1D(d, vertices.data(), vertices.size()));
  }

  if (indiciCount == 0 && componentCount != 0)
    indiciCount = vertices.size() / componentCount;

  if (opts.vertexAttributes) {
    setAttributeRamps(d,
        geom,
        "vertex",
        vertices.size(),
        opts.attributeMin,
        opts.attributeMax);
  }

  if (opts.primitiveAttributes) {
    setAttributeRamps(d,
        geom,
        "primitive",
        indiciCount,
        opts.attributeMin,
        opts.attributeMax);
  }

  anari::commitParameters(d, geom);
  return geom;
}

// --- Surfaces, materials, lights ---------------------------------------------

anari::Material makeMatteMaterial(anari::Device d, math::float3 color)
{
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::commitParameters(d, mat);
  return mat;
}

anari::Surface makeSurface(
    anari::Device d, anari::Geometry geometry, anari::Material material)
{
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", geometry);
  anari::setParameter(d, surface, "material", material);
  anari::commitParameters(d, surface);
  return surface;
}

anari::Light makeDirectionalLight(
    anari::Device d, math::float3 direction, float irradiance)
{
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", direction);
  anari::setParameter(d, light, "irradiance", irradiance);
  anari::commitParameters(d, light);
  return light;
}

anari::Light newLight(anari::Device d, const std::string &subtype)
{
  auto light = anari::newObject<anari::Light>(d, subtype.c_str());
  if (subtype == "hdri") {
    const size_t res = 64;
    auto hdr = scenes::TextureGenerator::generateCheckerBoardHDR(res);
    anari::setAndReleaseParameter(
        d, light, "radiance", anari::newArray2D(d, hdr.data(), res, res));
  }
  return light; // uncommitted: caller sets per-Case params, then commits
}

// --- Samplers ----------------------------------------------------------------

anari::Sampler makeCheckerboardSampler(anari::Device d, bool normalMap)
{
  auto sampler = anari::newObject<anari::Sampler>(d, "image2D");
  const size_t resolution = 64;
  auto image = normalMap
      ? scenes::TextureGenerator::generateCheckerBoardNormalMap(resolution)
      : scenes::TextureGenerator::generateCheckerBoard(resolution);
  anari::setAndReleaseParameter(d,
      sampler,
      "image",
      anari::newArray2D(d, image.data(), resolution, resolution));
  anari::commitParameters(d, sampler);
  return sampler;
}

anari::Sampler newImageSampler(anari::Device d,
    const std::string &subtype,
    const std::string &inAttribute,
    bool normalMap)
{
  auto sampler = anari::newObject<anari::Sampler>(d, subtype.c_str());

  if (subtype == "image1D") {
    auto img = scenes::TextureGenerator::generateGreyScale(16);
    anari::setAndReleaseParameter(
        d, sampler, "image", anari::newArray1D(d, img.data(), img.size()));
  } else if (subtype == "image2D") {
    const size_t res = 64;
    auto img = normalMap
        ? scenes::TextureGenerator::generateCheckerBoardNormalMap(res)
        : scenes::TextureGenerator::generateCheckerBoard(res);
    anari::setAndReleaseParameter(
        d, sampler, "image", anari::newArray2D(d, img.data(), res, res));
  } else if (subtype == "image3D") {
    const size_t res = 32;
    auto img = scenes::TextureGenerator::generateRGBRamp(res);
    anari::setAndReleaseParameter(
        d, sampler, "image", anari::newArray3D(d, img.data(), res, res, res));
  }

  if (!inAttribute.empty())
    anari::setParameter(d, sampler, "inAttribute", inAttribute.c_str());

  return sampler; // uncommitted: caller sets per-Case params, then commits
}

// --- Volumes -----------------------------------------------------------------

float radialDistanceField(math::float3 p)
{
  return math::length(p);
}

anari::SpatialField newStructuredRegularField(
    anari::Device d, std::array<uint32_t, 3> dimensions, const FieldFn &field)
{
  auto sf = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  const FieldFn &fn = field ? field : FieldFn(radialDistanceField);

  // Sample the field at each grid point, mapping the index along each axis to a
  // centered, normalized coordinate in [-1, 1] (a singleton axis sits at 0).
  auto coord = [](uint32_t i, uint32_t dim) {
    return dim > 1
        ? 2.f * static_cast<float>(i) / static_cast<float>(dim - 1) - 1.f
        : 0.f;
  };
  std::vector<float> data;
  data.reserve(
      static_cast<size_t>(dimensions[0]) * dimensions[1] * dimensions[2]);
  for (uint32_t z = 0; z < dimensions[2]; ++z)
    for (uint32_t y = 0; y < dimensions[1]; ++y)
      for (uint32_t x = 0; x < dimensions[0]; ++x)
        data.push_back(fn(math::float3(coord(x, dimensions[0]),
            coord(y, dimensions[1]),
            coord(z, dimensions[2]))));

  anari::setParameter(d, sf, "origin", math::float3(-1.f));
  anari::setParameter(d,
      sf,
      "spacing",
      math::float3(dimensions[0] ? 2.f / dimensions[0] : 0.f,
          dimensions[1] ? 2.f / dimensions[1] : 0.f,
          dimensions[2] ? 2.f / dimensions[2] : 0.f));
  anari::setParameterArray3D(
      d, sf, "data", data.data(), dimensions[0], dimensions[1], dimensions[2]);
  return sf; // uncommitted: caller may override origin/spacing/filter
}

anari::SpatialField makeStructuredRegularField(
    anari::Device d, std::array<uint32_t, 3> dimensions, const FieldFn &field)
{
  auto sf = newStructuredRegularField(d, dimensions, field);
  anari::commitParameters(d, sf);
  return sf;
}

anari::Volume makeVolume(anari::Device d, anari::SpatialField field)
{
  auto volume = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setParameter(d, volume, "value", field);

  std::vector<math::float3> colors = {
      {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}};
  std::vector<float> opacities = {0.f, 1.f};
  anari::setAndReleaseParameter(
      d, volume, "color", anari::newArray1D(d, colors.data(), colors.size()));
  anari::setAndReleaseParameter(d,
      volume,
      "opacity",
      anari::newArray1D(d, opacities.data(), opacities.size()));
  anari::setParameter(d, volume, "valueRange", math::float2(0.f, 1.f));
  anari::commitParameters(d, volume);
  return volume;
}

// --- Renderer ----------------------------------------------------------------

anari::Renderer newRenderer(anari::Device d, const std::string &subtype)
{
  return anari::newObject<anari::Renderer>(d, subtype.c_str());
}

// --- Instances ---------------------------------------------------------------

anari::Instance makeInstance(anari::Device d,
    const std::vector<anari::Surface> &surfaces,
    math::mat4 transform)
{
  auto group = anari::newObject<anari::Group>(d);
  if (!surfaces.empty()) {
    anari::setAndReleaseParameter(d,
        group,
        "surface",
        anari::newArray1D(d, surfaces.data(), surfaces.size()));
  }
  anari::commitParameters(d, group);

  auto inst = anari::newObject<anari::Instance>(d, "transform");
  float xfm[16];
  std::memcpy(xfm, &transform, sizeof(xfm));
  anari::setParameter(d, inst, "transform", xfm);
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::commitParameters(d, inst);
  return inst;
}

// --- Cameras -----------------------------------------------------------------

scenes::Camera cameraFromBounds(const scenes::Bounds &bounds)
{
  const math::float3 size = bounds[1] - bounds[0];
  const math::float3 center = 0.5f * (bounds[0] + bounds[1]);
  const float distance = math::length(size);
  const math::float3 eye = center + math::float3(0.f, 0.f, distance);

  scenes::Camera cam;
  cam.position = eye;
  cam.direction = math::normalize(center - eye);
  cam.at = center;
  cam.up = math::float3(0.f, 1.f, 0.f);
  return cam;
}

anari::Camera makePerspectiveCamera(anari::Device d,
    const scenes::Camera &camera,
    const PerspectiveCameraOptions &opts)
{
  auto cam = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, cam, "position", camera.position);
  anari::setParameter(d, cam, "direction", camera.direction);
  anari::setParameter(d, cam, "up", camera.up);
  if (opts.fovy.has_value())
    anari::setParameter(d, cam, "fovy", opts.fovy.value());
  if (opts.aspect.has_value())
    anari::setParameter(d, cam, "aspect", opts.aspect.value());
  if (opts.near.has_value())
    anari::setParameter(d, cam, "near", opts.near.value());
  if (opts.far.has_value())
    anari::setParameter(d, cam, "far", opts.far.value());
  anari::commitParameters(d, cam);
  return cam;
}

// --- World assembly + framing ------------------------------------------------

anari::World assembleWorld(anari::Device d, const WorldContents &contents)
{
  auto world = anari::newObject<anari::World>(d);

  if (!contents.surfaces.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "surface",
        anari::newArray1D(
            d, contents.surfaces.data(), contents.surfaces.size()));
  }
  if (!contents.volumes.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "volume",
        anari::newArray1D(d, contents.volumes.data(), contents.volumes.size()));
  }
  if (!contents.lights.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "light",
        anari::newArray1D(d, contents.lights.data(), contents.lights.size()));
  }
  if (!contents.instances.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "instance",
        anari::newArray1D(
            d, contents.instances.data(), contents.instances.size()));
  }

  anari::commitParameters(d, world);
  return world;
}

scenes::Bounds worldBounds(anari::Device d, anari::World world)
{
  scenes::Bounds b = {math::float3(-1.f), math::float3(1.f)};
  anari::getProperty(d, world, "bounds", b);
  return b;
}

anari::Frame makeColorFrame(
    anari::Device d, anari::World world, uint32_t width, uint32_t height)
{
  const auto bounds = worldBounds(d, world);
  const auto camDesc = cameraFromBounds(bounds);
  PerspectiveCameraOptions camOpts;
  if (height != 0)
    camOpts.aspect = static_cast<float>(width) / static_cast<float>(height);
  auto camera = makePerspectiveCamera(d, camDesc, camOpts);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", math::vec<uint32_t, 2>(width, height));
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "camera", camera);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::release(d, renderer);
  anari::release(d, camera);
  return frame;
}

} // namespace cts
} // namespace anari
