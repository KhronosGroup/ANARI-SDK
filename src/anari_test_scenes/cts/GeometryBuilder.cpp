// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "GeometryBuilder.h"
#include "generators/ColorPalette.h"
// std
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace scenes = anari::scenes;

namespace {

constexpr float kPitch = 0.5f;
constexpr float kTubeRadius = 0.3f * kPitch;
constexpr float kSphereRadius = 0.3f * kPitch;
constexpr float kCurveRadius = 0.1f * kPitch;

float ramp01(size_t index, size_t count)
{
  return count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1)
                   : 0.f;
}

void validateRadius(const std::optional<float> &radius, const char *family)
{
  if (radius && (!std::isfinite(*radius) || *radius <= 0.f)) {
    throw std::invalid_argument(
        std::string(family) + " global radius must be positive");
  }
}

void validateAttributes(const GeometryAttributes &attributes)
{
  switch (attributes.color) {
  case ColorAttribute::None:
  case ColorAttribute::Vertex:
  case ColorAttribute::Primitive:
    return;
  }
  throw std::invalid_argument("invalid color attribute enum value");
}

void setAttributeRamps(anari::Device d,
    anari::Geometry geometry,
    const char *prefix,
    size_t count,
    float minimum,
    float maximum)
{
  std::vector<float> a0(count);
  std::vector<math::float2> a1(count);
  std::vector<math::float3> a2(count);
  std::vector<math::float4> a3(count);
  for (size_t i = 0; i < count; ++i) {
    const float value = minimum + (maximum - minimum) * ramp01(i, count);
    a0[i] = value;
    a1[i] = math::float2(value);
    a2[i] = math::float3(value);
    a3[i] = math::float4(value);
  }
  const std::string p = prefix;
  anari::setAndReleaseParameter(d,
      geometry,
      (p + ".attribute0").c_str(),
      anari::newArray1D(d, a0.data(), a0.size()));
  anari::setAndReleaseParameter(d,
      geometry,
      (p + ".attribute1").c_str(),
      anari::newArray1D(d, a1.data(), a1.size()));
  anari::setAndReleaseParameter(d,
      geometry,
      (p + ".attribute2").c_str(),
      anari::newArray1D(d, a2.data(), a2.size()));
  anari::setAndReleaseParameter(d,
      geometry,
      (p + ".attribute3").c_str(),
      anari::newArray1D(d, a3.data(), a3.size()));
}

template <typename Index>
void publishIndices(anari::Device d,
    anari::Geometry geometry,
    const std::vector<Index> &indices)
{
  if (!indices.empty()) {
    anari::setAndReleaseParameter(d,
        geometry,
        "primitive.index",
        anari::newArray1D(d, indices.data(), indices.size()));
  }
}

void publishAttributes(anari::Device d,
    anari::Geometry geometry,
    const GeometryAttributes &attributes,
    size_t vertexCount,
    size_t primitiveCount)
{
  const char *colorParameter = nullptr;
  size_t colorCount = 0;
  switch (attributes.color) {
  case ColorAttribute::None:
    break;
  case ColorAttribute::Vertex:
    colorParameter = "vertex.color";
    colorCount = vertexCount;
    break;
  case ColorAttribute::Primitive:
    colorParameter = "primitive.color";
    colorCount = primitiveCount;
    break;
  default:
    throw std::invalid_argument("invalid color attribute enum value");
  }

  if (colorParameter) {
    auto colors = scenes::colors::getColorVectorFromPalette(colorCount);
    anari::setAndReleaseParameter(d,
        geometry,
        colorParameter,
        anari::newArray1D(d, colors.data(), colors.size()));
  }

  if (attributes.vertexRamps) {
    setAttributeRamps(d,
        geometry,
        "vertex",
        vertexCount,
        attributes.rampMinimum,
        attributes.rampMaximum);
  }
  if (attributes.primitiveRamps) {
    setAttributeRamps(d,
        geometry,
        "primitive",
        primitiveCount,
        attributes.rampMinimum,
        attributes.rampMaximum);
  }
}

void publishPositions(anari::Device d,
    anari::Geometry geometry,
    const std::vector<math::float3> &positions)
{
  anari::setAndReleaseParameter(d,
      geometry,
      "vertex.position",
      anari::newArray1D(d, positions.data(), positions.size()));
}

const char *capsName(CapsMode mode)
{
  switch (mode) {
  case CapsMode::None:
    return "none";
  case CapsMode::First:
    return "first";
  case CapsMode::Second:
    return "second";
  case CapsMode::Both:
    return "both";
  }
  throw std::invalid_argument("invalid caps mode enum value");
}

template <typename Spec>
void publishSegmentAttributes(anari::Device d,
    anari::Geometry geometry,
    const Spec &spec,
    const SegmentLayout &layout)
{
  const size_t primitiveCount = layout.indices.empty()
      ? layout.positions.size() / 2
      : layout.indices.size();
  publishAttributes(
      d, geometry, spec.attributes, layout.positions.size(), primitiveCount);
}

void publishVertexCaps(anari::Device d,
    anari::Geometry geometry,
    const std::optional<bool> &vertexCaps,
    size_t vertexCount)
{
  if (!vertexCaps)
    return;
  std::vector<uint8_t> caps(vertexCount, *vertexCaps ? 1u : 0u);
  anari::setAndReleaseParameter(d,
      geometry,
      "vertex.cap",
      anari::newArray1D(d, caps.data(), caps.size()));
}

} // namespace

ColorAttribute parseColorAttribute(std::string_view attribute)
{
  if (attribute.empty())
    return ColorAttribute::None;
  if (attribute == "vertex.color")
    return ColorAttribute::Vertex;
  if (attribute == "primitive.color")
    return ColorAttribute::Primitive;
  throw std::invalid_argument(
      "unsupported geometry color attribute '" + std::string(attribute) + "'");
}

CapsMode parseCapsMode(std::string_view mode)
{
  if (mode == "none")
    return CapsMode::None;
  if (mode == "first")
    return CapsMode::First;
  if (mode == "second")
    return CapsMode::Second;
  if (mode == "both")
    return CapsMode::Both;
  throw std::invalid_argument(
      "unknown geometry caps mode '" + std::string(mode) + "'");
}

anari::Geometry buildTriangleGeometry(anari::Device d, const TriangleSpec &spec)
{
  validateAttributes(spec.attributes);
  const auto layout = makeTriangleLayout(
      spec.shape, spec.mode, spec.primitiveCount, spec.unusedVertices);
  auto geometry = anari::newObject<anari::Geometry>(d, "triangle");
  publishIndices(d, geometry, layout.indices);
  publishPositions(d, geometry, layout.positions);
  const size_t primitiveCount = layout.indices.empty()
      ? layout.positions.size() / 3
      : layout.indices.size();
  publishAttributes(
      d, geometry, spec.attributes, layout.positions.size(), primitiveCount);
  anari::commitParameters(d, geometry);
  return geometry;
}

anari::Geometry buildQuadGeometry(anari::Device d, const QuadSpec &spec)
{
  validateAttributes(spec.attributes);
  const auto layout = makeQuadLayout(
      spec.shape, spec.mode, spec.primitiveCount, spec.unusedVertices);
  auto geometry = anari::newObject<anari::Geometry>(d, "quad");
  publishIndices(d, geometry, layout.indices);
  publishPositions(d, geometry, layout.positions);
  const size_t primitiveCount = layout.indices.empty()
      ? layout.positions.size() / 4
      : layout.indices.size();
  publishAttributes(
      d, geometry, spec.attributes, layout.positions.size(), primitiveCount);
  anari::commitParameters(d, geometry);
  return geometry;
}

anari::Geometry buildSphereGeometry(anari::Device d, const SphereSpec &spec)
{
  validateAttributes(spec.attributes);
  validateRadius(spec.globalRadius, "sphere");
  const auto layout =
      makeSphereLayout(spec.mode, spec.primitiveCount, spec.unusedVertices);
  auto geometry = anari::newObject<anari::Geometry>(d, "sphere");
  publishIndices(d, geometry, layout.indices);
  if (spec.globalRadius) {
    anari::setParameter(d, geometry, "radius", *spec.globalRadius);
  } else {
    std::vector<float> radii(layout.positions.size(), kSphereRadius);
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.radius",
        anari::newArray1D(d, radii.data(), radii.size()));
  }
  publishPositions(d, geometry, layout.positions);
  publishAttributes(d,
      geometry,
      spec.attributes,
      layout.positions.size(),
      spec.primitiveCount);
  anari::commitParameters(d, geometry);
  return geometry;
}

anari::Geometry buildCurveGeometry(anari::Device d, const CurveSpec &spec)
{
  validateAttributes(spec.attributes);
  validateRadius(spec.globalRadius, "curve");
  const auto layout =
      makeCurveLayout(spec.mode, spec.primitiveCount, spec.unusedVertices);
  auto geometry = anari::newObject<anari::Geometry>(d, "curve");
  publishIndices(d, geometry, layout.indices);
  if (spec.globalRadius) {
    anari::setParameter(d, geometry, "radius", *spec.globalRadius);
  } else {
    std::vector<float> radii(layout.positions.size(), kCurveRadius);
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.radius",
        anari::newArray1D(d, radii.data(), radii.size()));
  }
  publishPositions(d, geometry, layout.positions);
  const size_t primitiveCount =
      layout.indices.empty() ? layout.positions.size() : layout.indices.size();
  publishAttributes(
      d, geometry, spec.attributes, layout.positions.size(), primitiveCount);
  anari::commitParameters(d, geometry);
  return geometry;
}

anari::Geometry buildConeGeometry(anari::Device d, const ConeSpec &spec)
{
  validateAttributes(spec.attributes);
  const char *caps = capsName(spec.caps);
  const auto layout =
      makeSegmentLayout(spec.mode, spec.primitiveCount, spec.unusedVertices);
  auto geometry = anari::newObject<anari::Geometry>(d, "cone");
  publishIndices(d, geometry, layout.indices);
  std::vector<float> radii;
  radii.reserve(layout.positions.size());
  for (int i = 0; i < spec.primitiveCount; ++i) {
    radii.push_back(kTubeRadius);
    radii.push_back(0.f);
  }
  anari::setAndReleaseParameter(d,
      geometry,
      "vertex.radius",
      anari::newArray1D(d, radii.data(), radii.size()));
  publishVertexCaps(d, geometry, spec.vertexCaps, layout.positions.size());
  anari::setParameter(d, geometry, "caps", caps);
  publishPositions(d, geometry, layout.positions);
  publishSegmentAttributes(d, geometry, spec, layout);
  anari::commitParameters(d, geometry);
  return geometry;
}

anari::Geometry buildCylinderGeometry(anari::Device d, const CylinderSpec &spec)
{
  validateAttributes(spec.attributes);
  validateRadius(spec.globalRadius, "cylinder");
  const char *caps = capsName(spec.caps);
  const auto layout =
      makeSegmentLayout(spec.mode, spec.primitiveCount, spec.unusedVertices);
  auto geometry = anari::newObject<anari::Geometry>(d, "cylinder");
  publishIndices(d, geometry, layout.indices);
  if (spec.globalRadius) {
    anari::setParameter(d, geometry, "radius", *spec.globalRadius);
  } else {
    std::vector<float> radii(spec.primitiveCount, kTubeRadius);
    anari::setAndReleaseParameter(d,
        geometry,
        "primitive.radius",
        anari::newArray1D(d, radii.data(), radii.size()));
  }
  publishVertexCaps(d, geometry, spec.vertexCaps, layout.positions.size());
  anari::setParameter(d, geometry, "caps", caps);
  publishPositions(d, geometry, layout.positions);
  publishSegmentAttributes(d, geometry, spec, layout);
  anari::commitParameters(d, geometry);
  return geometry;
}

} // namespace cts
} // namespace anari
