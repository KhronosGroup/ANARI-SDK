// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Shared helpers for the helide-backed render-and-compare unit tests: render a
// world through a fixed camera and count pixels that differ between images.

#pragma once

#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
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

// Renders 'world' at kImageSize from (0, 0, 2) looking down -z with a black
// background, returning the float color channel.
inline std::vector<float4> render(
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
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);

  auto fb = anari::map<float4>(d, frame, "channel.color");
  std::vector<float4> pixels(fb.data, fb.data + fb.width * fb.height);
  anari::unmap(d, frame, "channel.color");

  anari::release(d, frame);
  return pixels;
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

} // namespace helide_test
