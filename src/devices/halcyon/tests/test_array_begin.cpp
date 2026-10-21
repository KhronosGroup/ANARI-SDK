// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// A 1D array's 'begin' and 'end' parameters select its elements [begin, end),
// and every reader of a 1D array honours them. Each case builds a scene once
// with plain arrays (the reference), then once per array parameter with only
// that array offset (junk elements before 'begin' and after 'end'), then with
// every array offset. Each offset scene must render the reference's colour and
// id channels exactly, without warnings.
//
// Also: committing only an array's 'begin'/'end' re-finalizes the objects
// using it, so the next render shows the new range.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr uint2 IMAGE_SIZE = {24, 24};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};

// Junk elements of an offset array before 'begin' and after 'end'
constexpr size_t PREFIX = 2;
constexpr size_t SUFFIX = 1;
// The junk elements' bytes: 0.747f as floats, 0x3F3F3F3F as uint32 (an index
// past every vertex), so reading them changes the image or warns
constexpr int JUNK_BYTE = 0x3F;

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
    std::fprintf(stderr, "[ANARI] WARNING: %s\n", message);
    g_warnings.push_back(message);
  }
}

bool check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

// A white matte surface of 'geometry', or coloured by 'color' (an attribute
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

// Sets a scene's 1D array parameters, offsetting the one named 'offset' (all
// of them if "*", none if empty)
struct Arrays
{
  anari::Device d;
  std::string offset;

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
    const size_t begin = offset ? PREFIX : 0;
    return {
        offset, begin, begin + count, begin + count + (offset ? SUFFIX : 0)};
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
    std::memset(bytes, JUNK_BYTE, l.capacity * size);
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
  // are decoy surfaces. The range is committed after the handles are written:
  // helium's ObjectArray::handlesBegin() adds 'begin' to handles an unmap
  // already copied from 'begin', so unmapping after committing a range would
  // read the wrong handles.
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

  // Commits range [begin, end) of 'array'
  void setRange(anari::Array1D array, size_t begin, size_t end) const
  {
    anari::setParameter(d, array, "begin", uint64_t(begin));
    anari::setParameter(d, array, "end", uint64_t(end));
    anari::commitParameters(d, array);
  }

 private:
  void setAndRelease(anari::Object o,
      const char *name,
      anari::Array1D array,
      const Layout &l) const
  {
    if (l.offset)
      setRange(array, l.begin, l.end);
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

// The channels a render produces
struct Image
{
  std::vector<float4> color;
  std::vector<uint32_t> primId;
  std::vector<uint32_t> objId;
  std::vector<uint32_t> instId;

  bool operator==(const Image &o) const
  {
    return color == o.color && primId == o.primId && objId == o.objId
        && instId == o.instId;
  }

  // pixels that aren't background
  size_t numHits() const
  {
    size_t n = 0;
    for (const auto &c : color)
      n += c != BACKGROUND;
    return n;
  }
};

template <typename T>
std::vector<T> readChannel(anari::Device d, anari::Frame f, const char *name)
{
  auto fb = anari::map<T>(d, f, name);
  std::vector<T> pixels(fb.data, fb.data + size_t(fb.width) * fb.height);
  anari::unmap(d, f, name);
  return pixels;
}

// A frame viewing [-1, 1]^2 down -z, with renderer 'mode'
anari::Frame makeFrame(anari::Device d, anari::World world, const char *mode)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "mode", mode);
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.primitiveId", ANARI_UINT32);
  anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
  anari::setParameter(d, frame, "channel.instanceId", ANARI_UINT32);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  return frame;
}

Image renderFrame(anari::Device d, anari::Frame frame)
{
  anari::render(d, frame);
  anari::wait(d, frame);
  Image image;
  image.color = readChannel<float4>(d, frame, "channel.color");
  image.primId = readChannel<uint32_t>(d, frame, "channel.primitiveId");
  image.objId = readChannel<uint32_t>(d, frame, "channel.objectId");
  image.instId = readChannel<uint32_t>(d, frame, "channel.instanceId");
  return image;
}

// 'world' rendered in each renderer mode that shows a case's arrays: eye-lit
// material colour, and the geometry's own 'attribute0' and 'attribute1'
// (which the material colour's 'vertex.color' hides)
std::vector<Image> render(anari::Device d, anari::World world)
{
  std::vector<Image> images;
  for (const char *mode :
      {"default", "geometry.attribute0", "geometry.attribute1"}) {
    auto frame = makeFrame(d, world, mode);
    images.push_back(renderFrame(d, frame));
    anari::release(d, frame);
  }
  return images;
}

// Geometry //////////////////////////////////////////////////////////////////

// A geometry subtype with every array it reads: positions, indices of
// 'components' values per primitive (also rendered without, as implicit
// primitives), optional radius array, and the attribute and id arrays below
struct GeometrySetup
{
  const char *subtype;
  std::vector<float3> positions;
  int components;
  std::vector<uint32_t> indices;
  const char *radiusName;
  std::vector<float> radii;
};

const std::vector<GeometrySetup> &geometrySetups()
{
  static const std::vector<GeometrySetup> retval = {
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
          {}},
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
          {}},
      {"sphere",
          {{-0.5f, -0.5f, 0.f},
              {0.5f, -0.5f, 0.f},
              {0.5f, 0.5f, 0.f},
              {-0.5f, 0.5f, 0.f}},
          1,
          {3, 0, 2},
          "vertex.radius",
          {0.2f, 0.3f, 0.4f, 0.25f}},
      {"curve",
          {{-0.8f, -0.6f, 0.f},
              {0.8f, -0.6f, 0.f},
              {0.8f, 0.6f, 0.f},
              {-0.8f, 0.6f, 0.f}},
          1,
          {0, 2},
          "vertex.radius",
          {0.1f, 0.2f, 0.15f, 0.25f}},
      {"cylinder",
          {{-0.8f, -0.6f, 0.f},
              {0.8f, -0.6f, 0.f},
              {0.8f, 0.6f, 0.f},
              {-0.8f, 0.6f, 0.f}},
          2,
          {0, 2, 1, 3},
          "primitive.radius",
          {0.2f, 0.3f}},
      {"cone",
          {{-0.8f, -0.6f, 0.f},
              {0.8f, -0.6f, 0.f},
              {0.8f, 0.6f, 0.f},
              {-0.8f, 0.6f, 0.f}},
          2,
          {0, 2, 1, 3},
          "vertex.radius",
          {0.1f, 0.3f, 0.2f, 0.05f}},
  };
  return retval;
}

// A distinct colour per vertex
std::vector<float4> vertexColors(size_t n)
{
  std::vector<float4> colors;
  for (size_t i = 0; i < n; i++) {
    const float f = float(i) / float(n);
    colors.push_back(float4(0.2f + 0.7f * f, 0.9f - 0.6f * f, 0.3f, 1.f));
  }
  return colors;
}

// Whether subtype 'subtype' reads face-varying attributes
bool hasFaceVarying(const char *subtype)
{
  return std::strcmp(subtype, "triangle") == 0
      || std::strcmp(subtype, "quad") == 0;
}

// 'n' shading normals, each different and none of them junk
std::vector<float3> normals(size_t n)
{
  std::vector<float3> retval;
  for (size_t i = 0; i < n; i++) {
    const float f = float(i) / float(n);
    retval.push_back(float3(0.6f * f - 0.3f, 0.3f - 0.5f * f, 1.f));
  }
  return retval;
}

// At least one per primitive of every setup
const std::vector<float4> PRIMITIVE_ATTRIBUTES = {{0.9f, 0.1f, 0.1f, 1.f},
    {0.1f, 0.9f, 0.1f, 1.f},
    {0.1f, 0.1f, 0.9f, 1.f},
    {0.9f, 0.9f, 0.1f, 1.f}};
const std::vector<uint32_t> PRIMITIVE_IDS = {11, 22, 33, 44};

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

// Whether 's' with 'indexing' gets 'faceVarying.normal' rather than
// 'vertex.normal' (see makeGeometryWorld())
bool hasFaceVaryingNormals(const GeometrySetup &s, Indexing indexing)
{
  return hasFaceVarying(s.subtype) && indexing == Indexing::UINT32;
}

// 's' with 'indexing'; 'vertexColor', if set, instead of the vertex colours
// through 'a'
anari::World makeGeometryWorld(const Arrays &a,
    const GeometrySetup &s,
    Indexing indexing,
    anari::Array1D vertexColor = nullptr)
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
  if (vertexColor)
    anari::setParameter(d, geom, "vertex.color", vertexColor);
  else
    a.set(geom, "vertex.color", vertexColors(s.positions.size()));
  a.set(geom, "primitive.attribute0", PRIMITIVE_ATTRIBUTES);
  a.set(geom, "primitive.id", PRIMITIVE_IDS);
  // one value per primitive corner, which 'indices' has as many of as the
  // setup has primitives, indexed or not; reversed, so they differ from the
  // vertex colours
  if (hasFaceVarying(s.subtype)) {
    auto values = vertexColors(s.indices.size());
    std::reverse(values.begin(), values.end());
    a.set(geom, "faceVarying.attribute1", values);
  }
  // shading normals, which the eye-lit mode shows; 'faceVarying.normal'
  // overrides 'vertex.normal', so UINT32-indexed setups get face-varying
  // normals and the others vertex normals
  if (hasFaceVaryingNormals(s, indexing)) {
    auto values = normals(s.indices.size());
    std::reverse(values.begin(), values.end());
    a.set(geom, "faceVarying.normal", values);
  } else if (hasFaceVarying(s.subtype)) {
    a.set(geom, "vertex.normal", normals(s.positions.size()));
  }
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

// makeSquare() coloured by 'sampler' (released)
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
anari::Volume makeVolume(anari::Device d,
    const std::vector<float> &voxels,
    float3 origin,
    float3 spacing)
{
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameterArray3D(d, field, "data", voxels.data(), 2, 2, 2);
  anari::setParameter(d, field, "origin", origin);
  anari::setParameter(d, field, "spacing", spacing);
  anari::commitParameters(d, field);
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
      makeVolume(d, voxels, float3(-1.f, -1.f, 0.1f), float3(2.f, 2.f, 1.f));
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
  auto volume =
      makeVolume(d, voxels, float3(-1.5f, -1.5f, -2.f), float3(3.f, 3.f, 1.f));
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

std::vector<Case> cases()
{
  std::vector<Case> retval;
  for (const auto &s : geometrySetups()) {
    for (Indexing indexing :
        {Indexing::IMPLICIT, Indexing::UINT32, Indexing::UINT64}) {
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
          {"vertex.color", "primitive.attribute0", "primitive.id", "surface"})
        c.arrays.push_back(name);
      if (hasFaceVarying(s.subtype))
        c.arrays.push_back("faceVarying.attribute1");
      if (hasFaceVaryingNormals(s, indexing))
        c.arrays.push_back("faceVarying.normal");
      else if (hasFaceVarying(s.subtype))
        c.arrays.push_back("vertex.normal");
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

// A case's renders and world bounds
struct Result
{
  std::vector<Image> images;
  std::vector<float> bounds;

  bool operator==(const Result &o) const
  {
    return images == o.images && bounds == o.bounds;
  }
};

Result renderCase(anari::Device d, const Case &c, const std::string &offset)
{
  auto world = c.build(Arrays{d, offset});
  Result r;
  r.images = render(d, world);
  r.bounds.resize(6);
  anariGetProperty(d,
      world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      r.bounds.data(),
      r.bounds.size() * sizeof(float),
      ANARI_WAIT);
  anari::release(d, world);
  return r;
}

bool testCase(anari::Device d, const Case &c)
{
  bool pass = true;
  const size_t warnings = g_warnings.size();
  const auto reference = renderCase(d, c, "");
  bool rendered = !reference.images.empty();
  for (const auto &image : reference.images)
    rendered &= image.numHits() > IMAGE_SIZE.x * IMAGE_SIZE.y / 10;
  pass &= check(rendered && g_warnings.size() == warnings,
      c.name + ": reference renders");

  std::vector<std::string> offsets = c.arrays;
  offsets.push_back("*");
  for (const auto &offset : offsets) {
    const size_t before = g_warnings.size();
    const bool same = renderCase(d, c, offset) == reference;
    pass &= check(same && g_warnings.size() == before,
        c.name + " offset " + (offset == "*" ? "all arrays" : offset)
            + ": same image and bounds");
  }
  return pass;
}

// Committing only 'begin'/'end' of a geometry's 'vertex.color' array changes
// the next render of the same frame
bool testRangeOnlyCommit(anari::Device d)
{
  const auto &s = geometrySetups()[0];
  const Arrays plain{d, ""};
  auto world = makeGeometryWorld(plain, s, Indexing::UINT32);
  const auto reference = render(d, world)[0];
  anari::release(d, world);

  // 'vertex.color' shows junk until its range starts past it
  const auto colors = vertexColors(s.positions.size());
  auto array = anari::newArray1D(d, ANARI_FLOAT32_VEC4, PREFIX + colors.size());
  auto *values = anari::map<float4>(d, array);
  std::memset(values, JUNK_BYTE, PREFIX * sizeof(float4));
  std::copy(colors.begin(), colors.end(), values + PREFIX);
  anari::unmap(d, array);
  plain.setRange(array, 0, colors.size());

  world = makeGeometryWorld(plain, s, Indexing::UINT32, array);

  auto frame = makeFrame(d, world, "default");
  bool pass = check(!(renderFrame(d, frame) == reference),
      "range-only commit: junk range renders differently");

  plain.setRange(array, PREFIX, PREFIX + colors.size());
  pass &= check(renderFrame(d, frame) == reference,
      "range-only commit: next render uses the new range");

  anari::release(d, frame);
  anari::release(d, world);
  anari::release(d, array);
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
  for (const auto &c : cases())
    pass &= testCase(d, c);
  pass &= testRangeOnlyCommit(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (!g_errors.empty())
    std::printf("FAIL: device reported errors\n");
  if (!g_warnings.empty())
    std::printf("FAIL: device reported warnings\n");

  return pass && g_errors.empty() && g_warnings.empty() ? 0 : 1;
}
