// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "random_curves.h"
// std
#include <random>

namespace anari {
namespace scenes {

RandomCurves::RandomCurves(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

RandomCurves::~RandomCurves()
{
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> RandomCurves::parameters()
{
  return {
      // clang-format off
      {makeParameterInfo("numCurves", "Number of curves to generate", 100, 1, int(1e5))},
      {makeParameterInfo("numSegments", "Number of segments in each curve", 16, 1, 1000)},
      {makeParameterInfo("radius", "Radius of all curves", 1e-2f)},
      {makeParameterInfo("randomizeRadii", "Per-vertex radii on the indexed curves", true)}
      // clang-format on
  };
}

anari::World RandomCurves::world()
{
  return m_world;
}

// Two curve geometries share one world so a single render covers both index
// paths: strands kept apart by 'primitive.index' (per-vertex radius when
// 'randomizeRadii' is set), and unindexed strands with the global radius.
void RandomCurves::commit()
{
  auto d = m_device;

  setDefaultLight(m_world);

  const int numCurves = getParam<int>("numCurves", 100);
  const int numSegments = getParam<int>("numSegments", 16);
  const float radius = getParam<float>("radius", 1e-2f);
  const bool randomizeRadii = getParam<bool>("randomizeRadii", true);

  if (numCurves < 1)
    throw std::runtime_error("'numCurves' must be >= 1");

  if (numSegments < 1)
    throw std::runtime_error("'numSegments' must be >= 1");

  if (radius <= 0.f)
    throw std::runtime_error("'radius' must be > 0.f");

  std::mt19937 rng;
  rng.seed(0);
  std::uniform_real_distribution<float> start_dist(0.f, 1.f);
  std::normal_distribution<float> step_dist(0.f, 0.04f);
  std::uniform_real_distribution<float> color_dist(0.f, 1.f);

  // draw x, y, z in a fixed order (argument evaluation order is unspecified)
  auto random3 = [&](auto &dist) {
    math::float3 v;
    v.x = dist(rng);
    v.y = dist(rng);
    v.z = dist(rng);
    return v;
  };

  // 'numStrands' random walks of 'numVertices' vertices each
  auto makeStrands = [&](int numStrands, int numVertices) {
    std::vector<math::float3> positions;
    positions.reserve(size_t(numStrands) * size_t(numVertices));
    for (int c = 0; c < numStrands; ++c) {
      math::float3 p = random3(start_dist);
      for (int v = 0; v < numVertices; ++v) {
        positions.push_back(p);
        p += random3(step_dist);
      }
    }
    return positions;
  };

  // a gradient between two random colors along each strand
  auto makeColors = [&](int numStrands, int numVertices) {
    std::vector<math::float4> colors;
    colors.reserve(size_t(numStrands) * size_t(numVertices));
    for (int c = 0; c < numStrands; ++c) {
      const math::float3 a = random3(color_dist);
      const math::float3 b = random3(color_dist);
      for (int v = 0; v < numVertices; ++v) {
        const float t = numVertices > 1 ? v / float(numVertices - 1) : 0.f;
        colors.emplace_back(a + (b - a) * t, 1.f);
      }
    }
    return colors;
  };

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);

  auto makeSurface = [&](anari::Geometry geom) {
    auto surface = anari::newObject<anari::Surface>(d);
    anari::setAndReleaseParameter(d, surface, "geometry", geom);
    anari::setParameter(d, surface, "material", mat);
    anari::commitParameters(d, surface);
    return surface;
  };

  const int verticesPerCurve = numSegments + 1;

  // 'numCurves' strands with per-vertex colors and the global radius
  auto makeCurves = [&](float xOffset) {
    auto positions = makeStrands(numCurves, verticesPerCurve);
    auto colors = makeColors(numCurves, verticesPerCurve);
    for (auto &p : positions)
      p.x += xOffset;

    auto geom = anari::newObject<anari::Geometry>(d, "curve");
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.position",
        anari::newArray1D(d, positions.data(), positions.size()));
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.color",
        anari::newArray1D(d, colors.data(), colors.size()));
    anari::setParameter(d, geom, "radius", radius);
    return geom;
  };

  // indexed: segments stay within a strand
  auto indexed = makeCurves(0.f);
  {
    std::vector<uint32_t> segmentStarts;
    segmentStarts.reserve(size_t(numCurves) * size_t(numSegments));
    for (int c = 0; c < numCurves; ++c) {
      for (int s = 0; s < numSegments; ++s)
        segmentStarts.push_back(uint32_t(c * verticesPerCurve + s));
    }
    anari::setAndReleaseParameter(d,
        indexed,
        "primitive.index",
        anari::newArray1D(d, segmentStarts.data(), segmentStarts.size()));

    if (randomizeRadii) {
      std::uniform_real_distribution<float> radii_dist(
          radius / 4.f, radius * 2.f);
      std::vector<float> radii(size_t(numCurves) * size_t(verticesPerCurve));
      for (auto &r : radii)
        r = radii_dist(rng);
      anari::setAndReleaseParameter(d,
          indexed,
          "vertex.radius",
          anari::newArray1D(d, radii.data(), radii.size()));
    }

    anari::commitParameters(d, indexed);
  }

  // unindexed: placed beside the indexed strands; with no index every vertex
  // starts a segment, so segments also join consecutive strands
  auto unindexed = makeCurves(1.25f);
  anari::commitParameters(d, unindexed);

  std::vector<anari::Surface> surfaces = {
      makeSurface(indexed), makeSurface(unindexed)};
  anari::setAndReleaseParameter(d,
      m_world,
      "surface",
      anari::newArray1D(d, surfaces.data(), surfaces.size()));
  anari::commitParameters(d, m_world);

  // cleanup

  for (auto s : surfaces)
    anari::release(d, s);
  anari::release(d, mat);
}

TestScene *sceneRandomCurves(anari::Device d)
{
  return new RandomCurves(d);
}

} // namespace scenes
} // namespace anari
