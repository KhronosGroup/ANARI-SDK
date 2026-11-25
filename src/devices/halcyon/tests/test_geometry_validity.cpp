// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Geometry validity through the device, where warnings are expected and
// recorded (the per-subtype geometry tests fail on any warning):
// - every subtype becomes invalid, and stops rendering, when 'vertex.position'
//   is unset after a valid commit, and renders again when it is set again;
//   only the geometry is committed again, not its surface, group or world
// - 'primitive.index' of every subtype may be UINT64 (with the subtype's
//   component count), as the spec allows: it renders the same image as UINT32
//   indices, with a warning when a value above UINT32_MAX is truncated; other
//   element types make the geometry invalid with a warning
// - sphere, curve, cylinder and cone clamp a 'primitive.index' past the last
//   vertex (segment, for curve) to the last one with one warning, and render
//   it, vertex colours included, like indices naming the last one
// The curve runs these with its 'basis' linear, B-spline and Catmull-Rom
// (HALCYON_GEOMETRY_CURVE_BASIS).

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

constexpr uint2 IMAGE_SIZE = {9, 9};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};

// Status messages at WARNING severity or worse
struct Messages
{
  std::vector<std::string> errors;
  std::vector<std::string> warnings;

  // whether a warning since the first 'from' ones contains 'text'
  bool warnedSince(size_t from, const char *text) const
  {
    return std::any_of(warnings.begin() + from,
        warnings.end(),
        [&](const std::string &w) { return w.find(text) != w.npos; });
  }
} g_messages;

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
    g_messages.errors.push_back(message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    g_messages.warnings.push_back(message);
  }
}

bool check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

// Orthographic camera looking down -z at the [-1, 1]^2 square
anari::Camera makeCamera(anari::Device d)
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

// A frame rendering 'world' in "default" mode
struct Scene
{
  anari::Device d;
  anari::World world;
  anari::Frame frame;

  // 'colorAttribute': the matte colour's attribute, else white
  Scene(anari::Device d,
      anari::Geometry geometry,
      const char *colorAttribute = nullptr)
      : d(d)
  {
    auto mat = anari::newObject<anari::Material>(d, "matte");
    if (colorAttribute)
      anari::setParameter(d, mat, "color", colorAttribute);
    else
      anari::setParameter(d, mat, "color", float3(1.f));
    anari::commitParameters(d, mat);
    auto surface = anari::newObject<anari::Surface>(d);
    anari::setParameter(d, surface, "geometry", geometry);
    anari::setAndReleaseParameter(d, surface, "material", mat);
    anari::commitParameters(d, surface);

    world = anari::newObject<anari::World>(d);
    anari::setParameterArray1D(d, world, "surface", &surface, 1);
    anari::release(d, surface);
    anari::commitParameters(d, world);

    auto renderer = anari::newObject<anari::Renderer>(d, "preview");
    anari::setParameter(d, renderer, "background", BACKGROUND);
    anari::commitParameters(d, renderer);

    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", IMAGE_SIZE);
    anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
    anari::setAndReleaseParameter(d, frame, "camera", makeCamera(d));
    anari::setAndReleaseParameter(d, frame, "renderer", renderer);
    anari::setParameter(d, frame, "world", world);
    anari::commitParameters(d, frame);
  }

  ~Scene()
  {
    anari::release(d, frame);
    anari::release(d, world);
  }

  std::vector<float4> render()
  {
    anari::render(d, frame);
    anari::wait(d, frame);
    auto fb = anari::map<float4>(d, frame, "channel.color");
    std::vector<float4> image(
        fb.data, fb.data + size_t(IMAGE_SIZE.x) * IMAGE_SIZE.y);
    anari::unmap(d, frame, "channel.color");
    return image;
  }

  // whether the pixel at world-space 'pos' shows a surface
  bool hit(float2 pos)
  {
    return hit(render(), pos);
  }

  static bool hit(const std::vector<float4> &image, float2 pos)
  {
    const uint2 px(uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x),
        uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y));
    const float4 c = image[size_t(px.y) * IMAGE_SIZE.x + px.x];
    const float4 diff = linalg::abs(c - BACKGROUND);
    return diff.x > 1e-3f || diff.y > 1e-3f || diff.z > 1e-3f;
  }
};

bool isValid(anari::Device d, anari::Object o)
{
  bool valid = false;
  anari::getProperty(d, o, "valid", valid, ANARI_WAIT);
  return valid;
}

// A subtype with positions covering the center of the view
struct SubtypeSetup
{
  const char *subtype;
  std::vector<float3> positions;
  // sets the radius parameters
  std::function<void(anari::Device, anari::Geometry)> setRadius;
  // the curve's 'basis' (HALCYON_GEOMETRY_CURVE_BASIS), if set
  const char *basis = nullptr;
};

// 'subtype' with 'basis' (if set) in test names
std::string setupName(const char *subtype, const char *basis)
{
  return basis ? std::string(subtype) + " " + basis : std::string(subtype);
}

// A new geometry of 'subtype' with 'basis' (if set)
anari::Geometry newGeometry(
    anari::Device d, const char *subtype, const char *basis)
{
  auto geom = anari::newObject<anari::Geometry>(d, subtype);
  if (basis)
    anari::setParameter(d, geom, "basis", basis);
  return geom;
}

const std::vector<SubtypeSetup> &subtypes()
{
  static const std::vector<SubtypeSetup> retval = {
      {"triangle",
          {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {0.f, 1.f, 0.f}},
          nullptr},
      {"quad",
          {{-1.f, -1.f, 0.f},
              {1.f, -1.f, 0.f},
              {1.f, 1.f, 0.f},
              {-1.f, 1.f, 0.f}},
          nullptr},
      {"sphere",
          {{0.f, 0.f, 0.f}},
          [](anari::Device d, anari::Geometry g) {
            anari::setParameter(d, g, "radius", 0.5f);
          }},
      {"curve",
          {{-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}},
          [](anari::Device d, anari::Geometry g) {
            anari::setParameter(d, g, "radius", 0.5f);
          }},
      // cubic segments over four collinear control points span the middle
      {"curve",
          {{-1.5f, 0.f, 0.f}, {-0.5f, 0.f, 0.f}, {0.5f, 0.f, 0.f}, {1.5f, 0.f, 0.f}},
          [](anari::Device d, anari::Geometry g) {
            anari::setParameter(d, g, "radius", 0.5f);
          },
          "bspline"},
      {"curve",
          {{-1.5f, 0.f, 0.f}, {-0.5f, 0.f, 0.f}, {0.5f, 0.f, 0.f}, {1.5f, 0.f, 0.f}},
          [](anari::Device d, anari::Geometry g) {
            anari::setParameter(d, g, "radius", 0.5f);
          },
          "catmullRom"},
      {"cylinder",
          {{-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}},
          [](anari::Device d, anari::Geometry g) {
            anari::setParameter(d, g, "radius", 0.5f);
          }},
      {"cone",
          {{-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}},
          [](anari::Device d, anari::Geometry g) {
            const float radii[] = {0.5f, 0.5f};
            anari::setParameterArray1D(d, g, "vertex.radius", radii, 2);
          }},
  };
  return retval;
}

bool testUnsetPosition(anari::Device d, const SubtypeSetup &setup)
{
  const std::string name = setupName(setup.subtype, setup.basis);
  bool pass = true;

  auto geom = newGeometry(d, setup.subtype, setup.basis);
  auto setPosition = [&]() {
    anari::setParameterArray1D(d,
        geom,
        "vertex.position",
        setup.positions.data(),
        setup.positions.size());
  };
  setPosition();
  if (setup.setRadius)
    setup.setRadius(d, geom);
  anari::commitParameters(d, geom);

  Scene scene(d, geom);
  pass &= check(isValid(d, geom) && scene.hit(float2(0.f)), name + " renders");

  const size_t warnings = g_messages.warnings.size();
  anari::unsetParameter(d, geom, "vertex.position");
  anari::commitParameters(d, geom);
  pass &= check(!isValid(d, geom), name + " without vertex.position: invalid");
  pass &= check(
      !scene.hit(float2(0.f)), name + " without vertex.position: not rendered");
  pass &= check(g_messages.warnedSince(warnings, "vertex.position"),
      name + " without vertex.position: warning");

  setPosition();
  anari::commitParameters(d, geom);
  pass &= check(isValid(d, geom) && scene.hit(float2(0.f)),
      name + " with vertex.position again: renders");

  anari::release(d, geom);
  return pass;
}

// One primitive through the center of the view, indexed by 'indices' (of
// 'components' values each) into 'positions' whose other vertices lie outside
// the view. Implicit indexing, indices read as 32-bit, or clamped instead of
// truncated indices would miss or colour the primitive differently.
struct IndexSetup
{
  const char *subtype;
  std::vector<float3> positions;
  int components;
  std::vector<uint64_t> indices;
  // sets the radius parameters
  std::function<void(anari::Device, anari::Geometry)> setRadius;
  // the curve's 'basis' (HALCYON_GEOMETRY_CURVE_BASIS), if set
  const char *basis = nullptr;
};

const std::vector<IndexSetup> &indexSetups()
{
  auto radius = [](float r) {
    return [=](anari::Device d, anari::Geometry g) {
      anari::setParameter(d, g, "radius", r);
    };
  };
  static const std::vector<IndexSetup> retval = {
      {"triangle",
          {{5.f, 5.f, 0.f},
              {-1.f, -1.f, 0.f},
              {1.f, -1.f, 0.f},
              {0.f, 1.f, 0.f},
              {5.f, -5.f, 0.f}},
          3,
          {1, 2, 3},
          nullptr},
      {"quad",
          {{5.f, 5.f, 0.f},
              {-1.f, -1.f, 0.f},
              {1.f, -1.f, 0.f},
              {1.f, 1.f, 0.f},
              {-1.f, 1.f, 0.f},
              {5.f, -5.f, 0.f}},
          4,
          {1, 2, 3, 4},
          nullptr},
      {"sphere",
          {{5.f, 5.f, 0.f}, {0.f, 0.f, 0.f}, {5.f, -5.f, 0.f}},
          1,
          {1},
          radius(0.5f)},
      // segment 1 runs through the origin, segment 2 (the last) doesn't
      {"curve",
          {{-0.8f, 0.5f, 0.f},
              {0.8f, 0.5f, 0.f},
              {-0.8f, -0.5f, 0.f},
              {0.8f, -0.5f, 0.f}},
          1,
          {1},
          radius(0.1f)},
      // the cubic segment over vertices 1 to 4 runs through the origin
      {"curve",
          {{5.f, 5.f, 0.f},
              {-1.5f, 0.f, 0.f},
              {-0.5f, 0.f, 0.f},
              {0.5f, 0.f, 0.f},
              {1.5f, 0.f, 0.f},
              {5.f, -5.f, 0.f}},
          1,
          {1},
          radius(0.1f),
          "bspline"},
      {"curve",
          {{5.f, 5.f, 0.f},
              {-1.5f, 0.f, 0.f},
              {-0.5f, 0.f, 0.f},
              {0.5f, 0.f, 0.f},
              {1.5f, 0.f, 0.f},
              {5.f, -5.f, 0.f}},
          1,
          {1},
          radius(0.1f),
          "catmullRom"},
      {"cylinder",
          {{5.f, 5.f, 0.f},
              {-1.f, 0.f, 0.f},
              {1.f, 0.f, 0.f},
              {5.f, -5.f, 0.f}},
          2,
          {1, 2},
          radius(0.5f)},
      {"cone",
          {{5.f, 5.f, 0.f},
              {-1.f, 0.f, 0.f},
              {1.f, 0.f, 0.f},
              {5.f, -5.f, 0.f}},
          2,
          {1, 2},
          [](anari::Device d, anari::Geometry g) {
            const float radii[] = {0.5f, 0.5f, 0.5f, 0.5f};
            anari::setParameterArray1D(d, g, "vertex.radius", radii, 4);
          }},
  };
  return retval;
}

// ANARI_UINT32 or ANARI_UINT64 ('wide') with 'components' components
anari::DataType indexType(bool wide, int components)
{
  const anari::DataType types32[] = {
      ANARI_UINT32, ANARI_UINT32_VEC2, ANARI_UINT32_VEC3, ANARI_UINT32_VEC4};
  const anari::DataType types64[] = {
      ANARI_UINT64, ANARI_UINT64_VEC2, ANARI_UINT64_VEC3, ANARI_UINT64_VEC4};
  return (wide ? types64 : types32)[components - 1];
}

// 'setup' as a geometry whose 'primitive.index' is 'indices' stored as
// 'type' (UINT32 or UINT64 with setup.components components), with a distinct
// 'vertex.color' per vertex
anari::Geometry makeIndexedGeometry(anari::Device d,
    const IndexSetup &setup,
    anari::DataType type,
    const std::vector<uint64_t> &indices)
{
  auto geom = newGeometry(d, setup.subtype, setup.basis);
  anari::setParameterArray1D(d,
      geom,
      "vertex.position",
      setup.positions.data(),
      setup.positions.size());
  std::vector<float3> colors;
  for (size_t i = 0; i < setup.positions.size(); i++)
    colors.push_back(
        float3(float(i & 1), float((i >> 1) & 1), float((i >> 2) & 1)) * 0.8f
        + 0.1f);
  anari::setParameterArray1D(
      d, geom, "vertex.color", colors.data(), colors.size());
  if (setup.setRadius)
    setup.setRadius(d, geom);

  const size_t numElements = indices.size() / setup.components;
  auto array = anari::newArray1D(d, type, numElements);
  void *data = anari::map<void>(d, array);
  const bool wide = anari::sizeOf(type) / anari::componentsOf(type) == 8;
  for (size_t i = 0; i < indices.size(); i++) {
    if (wide)
      static_cast<uint64_t *>(data)[i] = indices[i];
    else
      static_cast<uint32_t *>(data)[i] = uint32_t(indices[i]);
  }
  anari::unmap(d, array);
  anari::setAndReleaseParameter(d, geom, "primitive.index", array);

  anari::commitParameters(d, geom);
  return geom;
}

// 'setup' as a geometry whose 'primitive.index' is one zeroed element of
// 'type', which isn't a valid index type for it
anari::Geometry makeBadlyIndexedGeometry(
    anari::Device d, const IndexSetup &setup, anari::DataType type)
{
  auto geom = newGeometry(d, setup.subtype, setup.basis);
  anari::setParameterArray1D(d,
      geom,
      "vertex.position",
      setup.positions.data(),
      setup.positions.size());
  if (setup.setRadius)
    setup.setRadius(d, geom);
  auto array = anari::newArray1D(d, type, 1);
  std::memset(anari::map<void>(d, array), 0, anari::sizeOf(type));
  anari::unmap(d, array);
  anari::setAndReleaseParameter(d, geom, "primitive.index", array);
  anari::commitParameters(d, geom);
  return geom;
}

bool testIndexTypes(anari::Device d, const IndexSetup &setup)
{
  const std::string name = setupName(setup.subtype, setup.basis);
  const int n = setup.components;
  bool pass = true;

  // renders 'setup' with 'indices' in vertex colours; 'warnings' is the
  // warning count before
  auto renderIndexed = [&](const std::vector<uint64_t> &indices,
                           bool wide,
                           bool &valid,
                           size_t &warnings) {
    warnings = g_messages.warnings.size();
    auto geom = makeIndexedGeometry(d, setup, indexType(wide, n), indices);
    Scene scene(d, geom, "color");
    valid = isValid(d, geom);
    auto image = scene.render();
    anari::release(d, geom);
    return image;
  };

  bool valid = false;
  size_t warnings = 0;
  const auto reference = renderIndexed(setup.indices, false, valid, warnings);
  pass &= check(valid && Scene::hit(reference, float2(0.f))
          && g_messages.warnings.size() == warnings,
      name + " UINT32 primitive.index: renders");

  const auto image64 = renderIndexed(setup.indices, true, valid, warnings);
  pass &= check(
      valid && image64 == reference && g_messages.warnings.size() == warnings,
      name + " UINT64 primitive.index: same image as UINT32");

  // truncated to the UINT32 indices, not clamped to the last vertex
  std::vector<uint64_t> above = setup.indices;
  for (auto &i : above)
    i += uint64_t(1) << 32;
  const auto truncated = renderIndexed(above, true, valid, warnings);
  pass &= check(valid && truncated == reference,
      name + " UINT64 primitive.index above UINT32_MAX: truncated");
  pass &= check(g_messages.warnedSince(warnings, "truncated"),
      name + " UINT64 primitive.index above UINT32_MAX: warning");

  // FLOAT32, and UINT32 with the wrong component count
  const anari::DataType badTypes[] = {
      ANARI_FLOAT32, indexType(false, n == 1 ? 2 : 1)};
  for (anari::DataType type : badTypes) {
    const std::string typeName = anari::toString(type);
    warnings = g_messages.warnings.size();
    auto geom = makeBadlyIndexedGeometry(d, setup, type);
    Scene scene(d, geom);
    pass &= check(!isValid(d, geom) && !scene.hit(float2(0.f)),
        name + " " + typeName + " primitive.index: invalid");
    pass &= check(g_messages.warnedSince(warnings, "primitive.index"),
        name + " " + typeName + " primitive.index: warning");
    anari::release(d, geom);
  }

  return pass;
}

// A primitive through the center of the view whose 'inRange' indices name the
// last vertex (segment, for curve) of 'setup.positions', and 'pastEnd' indices
// that name one past the end instead
struct ClampSetup
{
  IndexSetup setup; // setup.indices: the in-range indices
  std::vector<uint64_t> pastEnd;
};

const std::vector<ClampSetup> &clampSetups()
{
  auto radius = [](float r) {
    return [=](anari::Device d, anari::Geometry g) {
      anari::setParameter(d, g, "radius", r);
    };
  };
  static const std::vector<ClampSetup> retval = {
      {{"sphere",
           {{5.f, 5.f, 0.f}, {5.f, -5.f, 0.f}, {0.f, 0.f, 0.f}},
           1,
           {2},
           radius(0.5f)},
          {3}},
      // the last segment (start 2) runs through the origin
      {{"curve",
           {{-0.8f, 0.5f, 0.f},
               {0.8f, 0.5f, 0.f},
               {-0.8f, 0.f, 0.f},
               {0.8f, 0.f, 0.f}},
           1,
           {2},
           radius(0.1f)},
          {3}},
      // the last cubic segment (start 1, of 5 vertices) runs through the
      // origin
      {{"curve",
           {{5.f, 5.f, 0.f},
               {-1.5f, 0.f, 0.f},
               {-0.5f, 0.f, 0.f},
               {0.5f, 0.f, 0.f},
               {1.5f, 0.f, 0.f}},
           1,
           {1},
           radius(0.1f),
           "bspline"},
          {2}},
      {{"curve",
           {{5.f, 5.f, 0.f},
               {-1.5f, 0.f, 0.f},
               {-0.5f, 0.f, 0.f},
               {0.5f, 0.f, 0.f},
               {1.5f, 0.f, 0.f}},
           1,
           {1},
           radius(0.1f),
           "catmullRom"},
          {2}},
      {{"cylinder",
           {{5.f, 5.f, 0.f}, {-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}},
           2,
           {1, 2},
           radius(0.5f)},
          {1, 3}},
      {{"cone",
           {{5.f, 5.f, 0.f}, {-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}},
           2,
           {1, 2},
           [](anari::Device d, anari::Geometry g) {
             const float radii[] = {0.5f, 0.5f, 0.5f};
             anari::setParameterArray1D(d, g, "vertex.radius", radii, 3);
           }},
          {1, 3}},
  };
  return retval;
}

bool testIndexClamp(anari::Device d, const ClampSetup &clamp)
{
  const IndexSetup &setup = clamp.setup;
  const char *warning = "past the";
  bool pass = true;

  for (bool wide : {false, true}) {
    const std::string name = setupName(setup.subtype, setup.basis)
        + (wide ? " UINT64" : " UINT32") + " primitive.index";
    const anari::DataType type = indexType(wide, setup.components);

    // renders 'indices' in vertex colours; 'warned' counts clamp warnings
    auto renderIndexed = [&](const std::vector<uint64_t> &indices,
                             bool &valid,
                             size_t &warned) {
      const size_t from = g_messages.warnings.size();
      auto geom = makeIndexedGeometry(d, setup, type, indices);
      Scene scene(d, geom, "color");
      valid = isValid(d, geom);
      auto image = scene.render();
      anari::release(d, geom);
      warned = std::count_if(g_messages.warnings.begin() + from,
          g_messages.warnings.end(),
          [&](const std::string &w) { return w.find(warning) != w.npos; });
      return image;
    };

    bool valid = false;
    size_t warned = 0;
    const auto reference = renderIndexed(setup.indices, valid, warned);
    pass &= check(
        valid && Scene::hit(reference, float2(0.f)) && warned == 0,
        name + " naming the last vertex: renders, no warning");

    const auto clamped = renderIndexed(clamp.pastEnd, valid, warned);
    pass &= check(valid && clamped == reference,
        name + " past the end: renders like the last vertex");
    pass &= check(warned == 1, name + " past the end: one warning");
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
  for (const auto &setup : subtypes())
    pass &= testUnsetPosition(d, setup);
  for (const auto &setup : indexSetups())
    pass &= testIndexTypes(d, setup);
  for (const auto &clamp : clampSetups())
    pass &= testIndexClamp(d, clamp);

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (!g_messages.errors.empty())
    std::printf("FAIL: device reported errors\n");

  return pass && g_messages.errors.empty() ? 0 : 1;
}
