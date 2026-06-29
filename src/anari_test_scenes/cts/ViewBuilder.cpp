// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ViewBuilder.h"
#include "WorldBuilder.h"
// std
#include <algorithm>

namespace anari {
namespace cts {

anari::Renderer newRenderer(anari::Device d, const std::string &subtype)
{
  return anari::newObject<anari::Renderer>(d, subtype.c_str());
}

scenes::Camera cameraFromBounds(const scenes::Bounds &bounds)
{
  const math::float3 size = bounds[1] - bounds[0];
  const math::float3 center = 0.5f * (bounds[0] + bounds[1]);
  constexpr float kTanHalfFovy = 0.5774f;
  constexpr float kMargin = 1.1f;
  const float halfInPlane = 0.5f * std::max(size.x, size.y);
  const float distance = kMargin * halfInPlane / kTanHalfFovy + 0.5f * size.z;
  const math::float3 eye = center + math::float3(0.f, 0.f, distance);

  scenes::Camera camera;
  camera.position = eye;
  camera.direction = math::normalize(center - eye);
  camera.at = center;
  camera.up = math::float3(0.f, 1.f, 0.f);
  return camera;
}

anari::Camera makePerspectiveCamera(anari::Device d,
    const scenes::Camera &camera,
    const PerspectiveCameraOptions &options)
{
  auto handle = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, handle, "position", camera.position);
  anari::setParameter(d, handle, "direction", camera.direction);
  anari::setParameter(d, handle, "up", camera.up);
  if (options.fovy)
    anari::setParameter(d, handle, "fovy", *options.fovy);
  if (options.aspect)
    anari::setParameter(d, handle, "aspect", *options.aspect);
  if (options.near)
    anari::setParameter(d, handle, "near", *options.near);
  if (options.far)
    anari::setParameter(d, handle, "far", *options.far);
  anari::commitParameters(d, handle);
  return handle;
}

anari::Frame makeColorFrame(
    anari::Device d, anari::World world, uint32_t width, uint32_t height)
{
  const auto cameraDescription = cameraFromBounds(worldBounds(d, world));
  PerspectiveCameraOptions cameraOptions;
  if (height != 0)
    cameraOptions.aspect = static_cast<float>(width) / height;
  auto camera = makePerspectiveCamera(d, cameraDescription, cameraOptions);
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
