// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Shared helpers for the helide-backed render-and-compare unit tests: render a
// world through a fixed camera, count pixels that differ between images,
// collect device warnings and errors, and build a simple volume or a triangle
// over a shared array.

#pragma once

#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
#include "catch.hpp"
// std
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace helide_test {

using namespace anari::math;

constexpr uint2 kImageSize = {64, 64};

// Status callback that prints errors and ignores everything else.
inline void printErrors(const void *,
    ANARIDevice,
    ANARIObject source,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ANARI][ERROR][%p] %s\n", source, message);
  }
}

// Warnings reported by the device, collected by collectWarnings() (the status
// callback runs on whichever thread finalizes the object).
struct WarningLog
{
  std::mutex mutex;
  std::vector<std::string> messages;

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex);
    messages.clear();
  }

  // Whether any warning contains 'text'.
  bool contains(const std::string &text)
  {
    std::lock_guard<std::mutex> lock(mutex);
    return std::any_of(messages.begin(), messages.end(), [&](auto &m) {
      return m.find(text) != std::string::npos;
    });
  }

  bool empty()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return messages.empty();
  }

  void add(const char *message)
  {
    std::lock_guard<std::mutex> lock(mutex);
    messages.push_back(message);
  }
};

// Status callback that prints errors (see printErrors()) and records warnings
// in the WarningLog passed as the library's user pointer.
inline void collectWarnings(const void *userPtr,
    ANARIDevice d,
    ANARIObject source,
    ANARIDataType type,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  printErrors(userPtr, d, source, type, severity, code, message);
  if (severity == ANARI_SEVERITY_WARNING)
    ((WarningLog *)userPtr)->add(message);
}

// Errors and warnings reported by the device, collected by collectStatus().
struct StatusLog
{
  WarningLog errors;
  WarningLog warnings;

  void clear()
  {
    errors.clear();
    warnings.clear();
  }
};

// Status callback that records errors and warnings, without printing them, in
// the StatusLog passed as the library's user pointer (for tests that expect
// errors).
inline void collectStatus(const void *userPtr,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  auto *log = (StatusLog *)userPtr;
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR)
    log->errors.add(message);
  else if (severity == ANARI_SEVERITY_WARNING)
    log->warnings.add(message);
}

// A spatial field of the 2x2x2 'voxels' (x fastest) at the corners of the box
// ['lower', 'upper'].
inline anari::SpatialField makeField(anari::Device d,
    const std::vector<float> &voxels,
    const float3 &lower,
    const float3 &upper)
{
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameterArray3D(d, field, "data", voxels.data(), 2, 2, 2);
  anari::setParameter(d, field, "origin", lower);
  anari::setParameter(d, field, "spacing", upper - lower);
  anari::commitParameters(d, field);
  return field;
}

// A spatial field filling the box ['lower', 'upper'] with the value 0.5.
inline anari::SpatialField makeConstantField(
    anari::Device d, const float3 &lower, const float3 &upper)
{
  return makeField(d, std::vector<float>(8, 0.5f), lower, upper);
}

// A translucent volume filling the box ['lower', 'upper'] with one color.
inline anari::Volume makeVolume(anari::Device d,
    const float3 &lower,
    const float3 &upper,
    const float3 &color,
    uint32_t id)
{
  auto field = makeConstantField(d, lower, upper);

  auto volume = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, volume, "value", field);
  anari::setParameter(d, volume, "color", color);
  anari::setParameter(d, volume, "opacity", 0.2f);
  anari::setParameter(d, volume, "id", id);
  anari::commitParameters(d, volume);
  return volume;
}

// One triangle covering the center of the view.
const float3 kCenterTriangle[3] = {
    {-0.5f, -0.5f, 0.f}, {0.5f, -0.5f, 0.f}, {0.f, 0.5f, 0.f}};

// A world holding one surface with a triangle geometry whose
// 'vertex.position' is a shared array over 'vertices' (3 elements). The app
// holds a reference to the world, surface and array; the surface holds the
// only one to the geometry.
struct TriangleWorld
{
  anari::World world{nullptr};
  anari::Surface surface{nullptr};
  anari::Array1D positions{nullptr};
  anari::Geometry geometry{nullptr};
};

inline TriangleWorld makeTriangleWorld(anari::Device d, const float3 *vertices)
{
  auto positions = anari::newArray1D(d, vertices, 3);

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameter(d, geom, "vertex.position", positions);
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  return {world, surface, positions, geom};
}

// Moves the triangle in 'vertices' out of view.
inline void moveOutOfView(float3 *vertices)
{
  for (int i = 0; i < 3; i++)
    vertices[i] = float3(100.f, 100.f, 100.f + i);
}

// Changes 'surface' (sets its default 'visible' explicitly) and commits it,
// which makes helide rebuild its Embree scenes from the geometry buffers at the
// next render.
inline void forceSceneRebuild(anari::Device d, anari::Surface surface)
{
  anari::setParameter(d, surface, "visible", true);
  anari::commitParameters(d, surface);
}

// An axis-aligned box, laid out as ANARI_FLOAT32_BOX3.
struct Box
{
  float3 lower;
  float3 upper;
};

// Queries 'world's "bounds" with ANARI_WAIT into 'bounds'; true if found.
inline bool queryBounds(anari::Device d, anari::World world, Box &bounds)
{
  return anariGetProperty(d,
      world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      &bounds,
      sizeof(bounds),
      ANARI_WAIT);
}

// The color, depth, objectId and instanceId channels of one rendered frame.
struct RenderResult
{
  std::vector<float4> color;
  std::vector<float> depth;
  std::vector<uint32_t> objectId;
  std::vector<uint32_t> instanceId;
};

// A frame viewing 'world' at kImageSize from (0, 0, 2) looking down -z with a
// black background, with color, depth, objectId and instanceId channels.
inline anari::Frame newFrame(
    anari::Device d, anari::World world, const std::string &mode = "default")
{
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::setParameter(d, renderer, "background", float4(0.f, 0.f, 0.f, 1.f));
  anari::setParameter(d, renderer, "mode", mode);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", kImageSize);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
  anari::setParameter(d, frame, "channel.instanceId", ANARI_UINT32);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  return frame;
}

// Waits for 'frame' and copies out its color, depth, objectId and instanceId
// channels.
inline RenderResult readChannels(anari::Device d, anari::Frame frame)
{
  anari::wait(d, frame);

  RenderResult result;

  auto color = anari::map<float4>(d, frame, "channel.color");
  result.color.assign(color.data, color.data + color.width * color.height);
  anari::unmap(d, frame, "channel.color");

  auto depth = anari::map<float>(d, frame, "channel.depth");
  result.depth.assign(depth.data, depth.data + depth.width * depth.height);
  anari::unmap(d, frame, "channel.depth");

  auto objectId = anari::map<uint32_t>(d, frame, "channel.objectId");
  result.objectId.assign(
      objectId.data, objectId.data + objectId.width * objectId.height);
  anari::unmap(d, frame, "channel.objectId");

  auto instanceId = anari::map<uint32_t>(d, frame, "channel.instanceId");
  result.instanceId.assign(
      instanceId.data, instanceId.data + instanceId.width * instanceId.height);
  anari::unmap(d, frame, "channel.instanceId");

  return result;
}

// Renders 'world' through newFrame(), returning the color, depth, objectId and
// instanceId channels.
inline RenderResult renderChannels(
    anari::Device d, anari::World world, const std::string &mode = "default")
{
  auto frame = newFrame(d, world, mode);
  anari::render(d, frame);
  auto result = readChannels(d, frame);
  anari::release(d, frame);
  return result;
}

// Renders 'world' as renderChannels() does, returning only the color channel.
inline std::vector<float4> render(
    anari::Device d, anari::World world, const std::string &mode = "default")
{
  return renderChannels(d, world, mode).color;
}

// Number of pixels whose channels differ by more than 1e-3.
inline size_t countMismatches(
    const std::vector<float4> &a, const std::vector<float4> &b)
{
  size_t mismatches = 0;
  for (size_t i = 0; i < a.size(); i++) {
    if (linalg::maxelem(linalg::abs(a[i] - b[i])) > 1e-3f)
      mismatches++;
  }
  return mismatches;
}

// Number of pixels whose depths differ by more than 1e-4 (equal depths match,
// including infinities and the FLT_MAX of pixels that hit nothing).
inline size_t countDepthMismatches(
    const std::vector<float> &a, const std::vector<float> &b)
{
  size_t mismatches = 0;
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i] != b[i] && !(std::abs(a[i] - b[i]) <= 1e-4f))
      mismatches++;
  }
  return mismatches;
}

// Number of pixels whose ids differ.
inline size_t countIdMismatches(
    const std::vector<uint32_t> &a, const std::vector<uint32_t> &b)
{
  size_t mismatches = 0;
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i] != b[i])
      mismatches++;
  }
  return mismatches;
}

// Checks that every channel of 'actual' matches 'expected'.
inline void checkSameImage(
    const RenderResult &actual, const RenderResult &expected)
{
  CHECK(countMismatches(actual.color, expected.color) == 0);
  CHECK(countDepthMismatches(actual.depth, expected.depth) == 0);
  CHECK(countIdMismatches(actual.objectId, expected.objectId) == 0);
  CHECK(countIdMismatches(actual.instanceId, expected.instanceId) == 0);
}

// Number of pixels whose id is 'id'.
inline size_t countId(const std::vector<uint32_t> &ids, uint32_t id)
{
  size_t n = 0;
  for (auto v : ids)
    n += v == id;
  return n;
}

// Whether a pixel of the given depth hit something: helide writes the depth
// of a pixel that hit nothing as FLT_MAX.
inline bool isHit(float depth)
{
  return depth < std::numeric_limits<float>::max();
}

// Number of pixels that hit something (see isHit()).
inline size_t countHits(const std::vector<float> &depth)
{
  return std::count_if(depth.begin(), depth.end(), isHit);
}

} // namespace helide_test
