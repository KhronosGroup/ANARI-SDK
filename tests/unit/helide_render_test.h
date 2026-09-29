// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Shared helpers for the helide-backed render-and-compare unit tests: render a
// world through a fixed camera and count pixels that differ between images.

#pragma once

#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdint>
#include <cstdio>
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

// The color, depth and objectId channels of one rendered frame.
struct RenderResult
{
  std::vector<float4> color;
  std::vector<float> depth;
  std::vector<uint32_t> objectId;
};

// Renders 'world' at kImageSize from (0, 0, 2) looking down -z with a black
// background, returning the color, depth and objectId channels.
inline RenderResult renderChannels(
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
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
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

// Number of pixels whose depths differ by more than 1e-4 (equal infinities,
// the depth of pixels that hit nothing, match).
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

} // namespace helide_test
