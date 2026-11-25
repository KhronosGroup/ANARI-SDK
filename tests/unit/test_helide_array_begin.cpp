// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// A 1D array's 'begin' and 'end' parameters select its elements [begin, end),
// and every helide reader of a 1D array honours them. Each case builds a scene
// once with plain arrays (the reference), then once per array parameter with
// only that array offset (junk elements before 'begin' and after 'end'), then
// with every array offset. Each offset scene must render the reference's
// channels and world bounds, without warnings or errors.
//
// Also: committing only an array's 'begin'/'end' re-finalizes the objects
// using it, so the next render of the same frame shows the new range.
//
// Ported from anari-halcyon's tests/test_array_begin.cpp.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace helide_test;

// Junk elements of an offset array before 'begin' and after 'end'
constexpr size_t kPrefix = 2;
constexpr size_t kSuffix = 1;
// The junk elements' bytes: 0.747f as floats, 0x3F3F3F3F as uint32 (an index
// past every vertex), so reading them changes the image or warns
constexpr int kJunkByte = 0x3F;

// A white matte surface of 'geometry', or colored by 'color' (an attribute
// name or a sampler)
template <typename T>
anari::Surface makeSurface(anari::Device d, anari::Geometry geometry, T color)
{
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::commitParameters(d, mat);
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", geometry);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", 5u);
  anari::commitParameters(d, surface);
  return surface;
}

// A red triangle in front of the whole view: the junk element of offset
// surface arrays
anari::Surface makeDecoySurface(anari::Device d)
{
  const float3 positions[] = {
      {-3.f, -3.f, 1.f}, {3.f, -3.f, 1.f}, {0.f, 3.f, 1.f}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", positions, 3);
  anari::commitParameters(d, geom);
  auto surface = makeSurface(d, geom, float3(1.f, 0.f, 0.f));
  anari::release(d, geom);
  return surface;
}

// Commits range [begin, end) of 'array'
void setRange(anari::Device d, anari::Array1D array, size_t begin, size_t end)
{
  anari::setParameter(d, array, "begin", uint64_t(begin));
  anari::setParameter(d, array, "end", uint64_t(end));
  anari::commitParameters(d, array);
}

// Sets a scene's 1D array parameters, offsetting the one named 'offset' (all
// of them if "*", none if empty)
struct Arrays
{
  anari::Device d;
  std::string offset;
  // If set, the offset array is committed with range [0, count) (junk first,
  // last elements cut off), and kept here, with a reference, for the caller to
  // commit its real range later, with its element count in 'keepCount'
  anari::Array1D *keep{nullptr};
  size_t *keepCount{nullptr};

  bool offsets(const char *name) const
  {
    return offset == "*" || offset == name;
  }

  // Where an array parameter's 'count' elements go: [begin, end) of
  // 'capacity', with junk around them if the parameter is offset
  struct Layout
  {
    bool offset;
    size_t begin;
    size_t end;
    size_t capacity;

    bool junk(size_t i) const
    {
      return i < begin || i >= end;
    }
  };

  Layout layout(const char *name, size_t count) const
  {
    const bool offset = offsets(name);
    const size_t begin = offset ? kPrefix : 0;
    return {
        offset, begin, begin + count, begin + count + (offset ? kSuffix : 0)};
  }

  // 'count' elements of 'type' at 'values' as array parameter 'name' of 'o'
  void set(anari::Object o,
      const char *name,
      anari::DataType type,
      const void *values,
      size_t count) const
  {
    const Layout l = layout(name, count);
    const size_t size = anari::sizeOf(type);
    auto array = anari::newArray1D(d, type, l.capacity);
    auto *bytes = static_cast<uint8_t *>(anari::map<void>(d, array));
    std::memset(bytes, kJunkByte, l.capacity * size);
    if (type == ANARI_FLOAT32_MAT4) {
      // All-0.747 matrices are singular: rendering with them in range would
      // hand Embree NaN rays, which debug Embree asserts on. Junk transforms
      // are invertible instead, and move their instance out of view.
      const mat4 junkXfm = linalg::translation_matrix(float3(100.f, 0.f, 0.f));
      for (size_t i = 0; i < l.capacity; i++) {
        if (l.junk(i))
          std::memcpy(bytes + i * size, &junkXfm, size);
      }
    }
    std::memcpy(bytes + l.begin * size, values, count * size);
    anari::unmap(d, array);
    setAndRelease(o, name, array, l);
  }

  template <typename T>
  void set(
      anari::Object o, const char *name, const std::vector<T> &values) const
  {
    set(o, name, anari::ANARITypeFor<T>::value, values.data(), values.size());
  }

  // 'surfaces' as parameter 'name' of 'o'; an offset array's junk elements
  // are decoy surfaces
  void setSurfaces(anari::Object o,
      const char *name,
      const std::vector<anari::Surface> &surfaces) const
  {
    const Layout l = layout(name, surfaces.size());
    auto decoy = makeDecoySurface(d);
    auto array = anari::newArray1D(d, ANARI_SURFACE, l.capacity);
    auto *handles = anari::map<anari::Surface>(d, array);
    for (size_t i = 0; i < l.capacity; i++)
      handles[i] = l.junk(i) ? decoy : surfaces[i - l.begin];
    anari::unmap(d, array);
    setAndRelease(o, name, array, l);
    anari::release(d, decoy);
  }

 private:
  void setAndRelease(anari::Object o,
      const char *name,
      anari::Array1D array,
      const Layout &l) const
  {
    if (l.offset && keep) {
      setRange(d, array, 0, l.end - l.begin);
      anari::retain(d, array);
      *keep = array;
      *keepCount = l.end - l.begin;
    } else if (l.offset) {
      setRange(d, array, l.begin, l.end);
    }
    anari::setAndReleaseParameter(d, o, name, array);
  }
};

// A world of 'surface' (released)
anari::World makeWorld(const Arrays &a, anari::Surface surface)
{
  auto world = anari::newObject<anari::World>(a.d);
  a.setSurfaces(world, "surface", {surface});
  anari::release(a.d, surface);
  anari::commitParameters(a.d, world);
  return world;
}

// The renderer modes that show a case's arrays: eye-lit material color, and
// the geometry's own 'attribute0' (which the material color's 'vertex.color'
// hides)
const char *const kModes[] = {"default", "geometry.attribute0"};

// Geometry //////////////////////////////////////////////////////////////////

// A geometry subtype with every array it reads: positions, indices of
// 'components' values per primitive (also rendered without, as implicit
// primitives), optional radius array, and the attribute arrays below
struct GeometrySetup
{
  const char *subtype;
  std::vector<float3> positions;
  int components;
  std::vector<uint32_t> indices;
  const char *radiusName;
  std::vector<float> radii;
  // whether helide reads UINT64 'primitive.index' elements for this subtype
  bool uint64Indices;
};

const std::vector<GeometrySetup> kGeometrySetups = {
    {"triangle",
        {{-0.9f, -0.9f, 0.f},
            {0.9f, -0.9f, 0.f},
            {0.9f, 0.9f, 0.f},
            {-0.9f, -0.8f, 0.f},
            {0.8f, 0.9f, 0.f},
            {-0.9f, 0.9f, 0.f}},
        3,
        {0, 1, 2, 0, 2, 5},
        nullptr,
        {},
        false},
    {"quad",
        {{-0.9f, -0.9f, 0.f},
            {0.f, -0.9f, 0.f},
            {0.f, 0.f, 0.f},
            {-0.9f, 0.f, 0.f},
            {0.1f, 0.1f, 0.f},
            {0.9f, 0.f, 0.f},
            {0.9f, 0.9f, 0.f},
            {0.f, 0.9f, 0.f}},
        4,
        {0, 1, 2, 3, 2, 5, 6, 7},
        nullptr,
        {},
        false},
    {"sphere",
        {{-0.5f, -0.5f, 0.f},
            {0.5f, -0.5f, 0.f},
            {0.5f, 0.5f, 0.f},
            {-0.5f, 0.5f, 0.f}},
        1,
        {3, 0, 2},
        "vertex.radius",
        {0.2f, 0.3f, 0.4f, 0.25f},
        true},
    {"curve",
        {{-0.8f, -0.6f, 0.f},
            {0.8f, -0.6f, 0.f},
            {0.8f, 0.6f, 0.f},
            {-0.8f, 0.6f, 0.f}},
        1,
        {0, 2},
        "vertex.radius",
        {0.1f, 0.2f, 0.15f, 0.25f},
        true},
    {"cylinder",
        {{-0.8f, -0.6f, 0.f},
            {0.8f, -0.6f, 0.f},
            {0.8f, 0.6f, 0.f},
            {-0.8f, 0.6f, 0.f}},
        2,
        {0, 2, 1, 3},
        "primitive.radius",
        {0.2f, 0.3f},
        false},
    {"cone",
        {{-0.8f, -0.6f, 0.f},
            {0.8f, -0.6f, 0.f},
            {0.8f, 0.6f, 0.f},
            {-0.8f, 0.6f, 0.f}},
        2,
        {0, 2, 1, 3},
        "vertex.radius",
        {0.1f, 0.3f, 0.2f, 0.05f},
        false},
};

// A distinct color per vertex
std::vector<float4> vertexColors(size_t n)
{
  std::vector<float4> colors;
  for (size_t i = 0; i < n; i++) {
    const float f = float(i) / float(n);
    colors.push_back(float4(0.2f + 0.7f * f, 0.9f - 0.6f * f, 0.3f, 1.f));
  }
  return colors;
}

// At least one per primitive of every setup
const std::vector<float4> kPrimitiveAttributes = {{0.9f, 0.1f, 0.1f, 1.f},
    {0.1f, 0.9f, 0.1f, 1.f},
    {0.1f, 0.1f, 0.9f, 1.f},
    {0.9f, 0.9f, 0.1f, 1.f}};

enum class Indexing
{
  IMPLICIT,
  UINT32,
  UINT64
};

anari::DataType indexType(Indexing indexing, int components)
{
  const anari::DataType types32[] = {
      ANARI_UINT32, ANARI_UINT32_VEC2, ANARI_UINT32_VEC3, ANARI_UINT32_VEC4};
  const anari::DataType types64[] = {
      ANARI_UINT64, ANARI_UINT64_VEC2, ANARI_UINT64_VEC3, ANARI_UINT64_VEC4};
  return (indexing == Indexing::UINT64 ? types64 : types32)[components - 1];
}

// 's' with 'indexing', a 'vertex.color' and a 'primitive.attribute0'
anari::World makeGeometryWorld(
    const Arrays &a, const GeometrySetup &s, Indexing indexing)
{
  auto d = a.d;
  auto geom = anari::newObject<anari::Geometry>(d, s.subtype);
  a.set(geom, "vertex.position", s.positions);
  if (indexing == Indexing::UINT32) {
    a.set(geom,
        "primitive.index",
        indexType(indexing, s.components),
        s.indices.data(),
        s.indices.size() / s.components);
  } else if (indexing == Indexing::UINT64) {
    const std::vector<uint64_t> indices(s.indices.begin(), s.indices.end());
    a.set(geom,
        "primitive.index",
        indexType(indexing, s.components),
        indices.data(),
        indices.size() / s.components);
  }
  if (s.radiusName)
    a.set(geom, s.radiusName, s.radii);
  a.set(geom, "vertex.color", vertexColors(s.positions.size()));
  a.set(geom, "primitive.attribute0", kPrimitiveAttributes);
  anari::commitParameters(d, geom);

  auto surface = makeSurface(d, geom, "color");
  anari::release(d, geom);
  return makeWorld(a, surface);
}

// Samplers, volumes, instances ///////////////////////////////////////////////

// Two triangles covering most of the view, with 'vertex.attribute0' a
// different u per vertex
anari::Geometry makeSquare(const Arrays &a)
{
  const std::vector<float3> positions = {{-0.9f, -0.9f, 0.f},
      {0.9f, -0.9f, 0.f},
      {0.9f, 0.9f, 0.f},
      {-0.9f, 0.9f, 0.f}};
  const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
  const std::vector<float> u = {0.f, 1.f, 0.7f, 0.2f};
  auto geom = anari::newObject<anari::Geometry>(a.d, "triangle");
  a.set(geom, "vertex.position", positions);
  a.set(geom, "primitive.index", ANARI_UINT32_VEC3, indices.data(), 2);
  a.set(geom, "vertex.attribute0", u);
  anari::commitParameters(a.d, geom);
  return geom;
}

// makeSquare() colored by 'sampler' (released)
anari::World makeSampledWorld(const Arrays &a, anari::Sampler sampler)
{
  auto geom = makeSquare(a);
  auto surface = makeSurface(a.d, geom, sampler);
  anari::release(a.d, geom);
  anari::release(a.d, sampler);
  return makeWorld(a, surface);
}

anari::World makePrimitiveSamplerWorld(const Arrays &a)
{
  auto sampler = anari::newObject<anari::Sampler>(a.d, "primitive");
  a.set(sampler,
      "array",
      std::vector<float4>{{0.9f, 0.2f, 0.1f, 1.f}, {0.1f, 0.3f, 0.8f, 1.f}});
  anari::commitParameters(a.d, sampler);
  return makeSampledWorld(a, sampler);
}

anari::World makeImage1DSamplerWorld(const Arrays &a)
{
  auto sampler = anari::newObject<anari::Sampler>(a.d, "image1D");
  a.set(sampler,
      "image",
      std::vector<float4>{{0.9f, 0.2f, 0.1f, 1.f},
          {0.1f, 0.8f, 0.2f, 1.f},
          {0.1f, 0.3f, 0.8f, 1.f},
          {0.8f, 0.8f, 0.8f, 1.f}});
  anari::setParameter(a.d, sampler, "inAttribute", "attribute0");
  anari::commitParameters(a.d, sampler);
  return makeSampledWorld(a, sampler);
}

// A transferFunction1D volume of a 2^3 'voxels' field over the box at
// 'origin' with size 'spacing', with 'opacity' 0.5; the caller sets the rest
// and commits
anari::Volume makeTfVolume(anari::Device d,
    const std::vector<float> &voxels,
    float3 origin,
    float3 spacing)
{
  auto field = makeField(d, voxels, origin, origin + spacing);
  auto volume = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, volume, "value", field);
  anari::setParameter(d, volume, "opacity", 0.5f);
  return volume;
}

// A transferFunction1D volume over the whole view, with 'color' and 'opacity'
// arrays, in front of makeSquare()
anari::World makeTransferFunctionWorld(const Arrays &a)
{
  auto d = a.d;
  const std::vector<float> voxels = {
      0.f, 0.3f, 0.6f, 1.f, 0.2f, 0.9f, 0.4f, 0.7f};
  auto volume =
      makeTfVolume(d, voxels, float3(-1.f, -1.f, 0.1f), float3(2.f, 2.f, 1.f));
  a.set(volume,
      "color",
      std::vector<float3>{
          {0.9f, 0.1f, 0.1f}, {0.1f, 0.9f, 0.1f}, {0.1f, 0.1f, 0.9f}});
  a.set(volume, "opacity", std::vector<float>{0.1f, 0.9f, 0.4f});
  anari::setParameter(d, volume, "id", 8u);
  anari::commitParameters(d, volume);

  auto geom = makeSquare(a);
  auto surface = makeSurface(d, geom, float3(1.f));
  anari::release(d, geom);
  auto world = makeWorld(a, surface);
  anari::setParameterArray1D(d, world, "volume", &volume, 1);
  anari::release(d, volume);
  anari::commitParameters(d, world);
  return world;
}

// Two instances (a transform array) of a group of makeSquare(), each with its
// own 'id' and 'color', and a volume behind it (world bounds include the
// volume's box under each transform)
anari::World makeInstanceWorld(const Arrays &a)
{
  auto d = a.d;
  const std::vector<float> voxels(8, 0.5f);
  auto volume = makeTfVolume(
      d, voxels, float3(-1.5f, -1.5f, -2.f), float3(3.f, 3.f, 1.f));
  anari::commitParameters(d, volume);

  auto geom = makeSquare(a);
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "volume", &volume, 1);
  anari::release(d, volume);
  auto surface = makeSurface(d, geom, "color");
  anari::release(d, geom);
  a.setSurfaces(group, "surface", {surface});
  anari::release(d, surface);
  anari::commitParameters(d, group);

  const std::vector<mat4> xfms = {
      linalg::mul(linalg::translation_matrix(float3(-0.5f, 0.f, 0.f)),
          linalg::scaling_matrix(float3(0.4f, 0.8f, 1.f))),
      linalg::mul(linalg::translation_matrix(float3(0.5f, 0.2f, 0.f)),
          linalg::scaling_matrix(float3(0.3f, 0.5f, 1.f)))};
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, inst, "group", group);
  a.set(inst, "transform", xfms);
  a.set(inst, "id", std::vector<uint32_t>{7, 9});
  a.set(inst,
      "color",
      std::vector<float4>{{0.9f, 0.2f, 0.1f, 1.f}, {0.1f, 0.3f, 0.8f, 1.f}});
  anari::commitParameters(d, inst);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &inst, 1);
  anari::release(d, inst);
  anari::commitParameters(d, world);
  return world;
}

// Cases //////////////////////////////////////////////////////////////////////

struct Case
{
  std::string name;
  // the array parameters build() sets through its Arrays
  std::vector<std::string> arrays;
  std::function<anari::World(const Arrays &)> build;
};

std::vector<Case> makeCases()
{
  std::vector<Case> retval;
  for (const auto &s : kGeometrySetups) {
    for (Indexing indexing :
        {Indexing::IMPLICIT, Indexing::UINT32, Indexing::UINT64}) {
      if (indexing == Indexing::UINT64 && !s.uint64Indices)
        continue;
      Case c;
      c.name = std::string(s.subtype)
          + (indexing == Indexing::IMPLICIT      ? ""
                  : indexing == Indexing::UINT32 ? " UINT32-indexed"
                                                 : " UINT64-indexed");
      c.arrays = {"vertex.position"};
      if (indexing != Indexing::IMPLICIT)
        c.arrays.push_back("primitive.index");
      if (s.radiusName)
        c.arrays.push_back(s.radiusName);
      for (const char *name :
          {"vertex.color", "primitive.attribute0", "surface"})
        c.arrays.push_back(name);
      c.build = [&s, indexing](const Arrays &a) {
        return makeGeometryWorld(a, s, indexing);
      };
      retval.push_back(c);
    }
  }
  const std::vector<std::string> square = {
      "vertex.position", "primitive.index", "vertex.attribute0", "surface"};
  auto withSquare = [&](std::vector<std::string> names) {
    names.insert(names.end(), square.begin(), square.end());
    return names;
  };
  retval.push_back(
      {"primitive sampler", withSquare({"array"}), makePrimitiveSamplerWorld});
  retval.push_back(
      {"image1D sampler", withSquare({"image"}), makeImage1DSamplerWorld});
  retval.push_back({"transferFunction1D",
      withSquare({"color", "opacity"}),
      makeTransferFunctionWorld});
  retval.push_back({"instance",
      withSquare({"transform", "id", "color"}),
      makeInstanceWorld});
  return retval;
}

// A case's renders (one per kModes) and world bounds
struct Result
{
  std::vector<RenderResult> images;
  Box bounds{};
};

Result renderWorld(anari::Device d, anari::World world)
{
  Result r;
  for (const char *mode : kModes)
    r.images.push_back(renderChannels(d, world, mode));
  queryBounds(d, world, r.bounds);
  return r;
}

Result renderCase(anari::Device d, const Case &c, const std::string &offset)
{
  auto world = c.build(Arrays{d, offset});
  auto r = renderWorld(d, world);
  anari::release(d, world);
  return r;
}

void checkSameResult(const Result &actual, const Result &expected)
{
  REQUIRE(actual.images.size() == expected.images.size());
  for (size_t i = 0; i < actual.images.size(); i++) {
    INFO("mode " << kModes[i]);
    checkSameImage(actual.images[i], expected.images[i]);
  }
  CHECK(actual.bounds.lower == expected.bounds.lower);
  CHECK(actual.bounds.upper == expected.bounds.upper);
}

// Whether every channel of 'a' matches 'b' (see checkSameImage())
bool sameImage(const RenderResult &a, const RenderResult &b)
{
  return countMismatches(a.color, b.color) == 0
      && countDepthMismatches(a.depth, b.depth) == 0
      && countIdMismatches(a.objectId, b.objectId) == 0
      && countIdMismatches(a.instanceId, b.instanceId) == 0;
}

} // namespace

TEST_CASE("helide reads every 1D array from its 'begin'",
    "[helide][helide_array_begin]")
{
  StatusLog status;
  anari::Library lib = anari::loadLibrary("helide", collectStatus, &status);
  if (lib == nullptr) {
    WARN("helide library not available; skipping array begin test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  SECTION("offset arrays render as plain arrays")
  {
    for (const auto &c : makeCases()) {
      INFO("case: " << c.name);
      status.clear();
      const auto reference = renderCase(d, c, "");
      CHECK(status.errors.empty());
      CHECK(status.warnings.empty());
      for (const auto &image : reference.images)
        CHECK(countHits(image.depth) > kImageSize.x * kImageSize.y / 10);

      auto offsets = c.arrays;
      offsets.push_back("*");
      for (const auto &offset : offsets) {
        INFO("offset: " << (offset == "*" ? "all arrays" : offset));
        status.clear();
        checkSameResult(renderCase(d, c, offset), reference);
        CHECK(status.errors.empty());
        CHECK(status.warnings.empty());
      }
    }
  }

  SECTION("committing only an array's range changes the next render")
  {
    // Arrays copied at finalize (instance 'transform' and its inverses),
    // shared with Embree ('vertex.position') and read at render time (the
    // rest). 'primitive.index' is left out: its junk range would hand Embree
    // out-of-range indices.
    const auto cases = makeCases();
    const std::vector<std::pair<std::string, std::vector<std::string>>>
        rangeCases = {{"triangle", {"vertex.position", "vertex.color"}},
            {"instance", {"transform", "id", "color"}}};
    for (const auto &[caseName, names] : rangeCases) {
      const auto &c = *std::find_if(cases.begin(),
          cases.end(),
          [&](const Case &c) { return c.name == caseName; });
      INFO("case: " << c.name);
      const auto reference = renderCase(d, c, "");

      for (const auto &name : names) {
        INFO("array: " << name);
        anari::Array1D array = nullptr;
        size_t count = 0;
        auto world = c.build(Arrays{d, name, &array, &count});
        REQUIRE(array != nullptr);

        auto frame = newFrame(d, world);
        anari::render(d, frame);
        CHECK(!sameImage(readChannels(d, frame), reference.images[0]));

        setRange(d, array, kPrefix, kPrefix + count);
        anari::render(d, frame);
        checkSameImage(readChannels(d, frame), reference.images[0]);

        anari::release(d, frame);
        anari::release(d, world);
        anari::release(d, array);
      }
    }
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
