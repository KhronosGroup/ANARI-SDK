// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Hdri.h"
// std
#include <algorithm>
#include <cmath>
#include <string>

namespace helide_gpu {

static vec3 normalizedOrZero(const vec3 &v)
{
  const float len = glm::length(v);
  return std::isfinite(len) && len > 0.f ? v / len : vec3(0.f);
}

Hdri::Hdri(HelideGPUDeviceGlobalState *s) : Light(s, LightType::HDRI) {}

Hdri::~Hdri()
{
  gpu_enqueue_method(this, &Hdri::gpu_freeResources).wait();
}

bool Hdri::frame(const vec3 &up,
    const vec3 &direction,
    vec3 &left,
    vec3 &upOut,
    vec3 &directionOut)
{
  upOut = normalizedOrZero(up);
  const vec3 d = normalizedOrZero(direction);
  if (upOut == vec3(0.f) || d == vec3(0.f))
    return false;
  const vec3 perpendicular = d - glm::dot(d, upOut) * upOut;
  if (!(glm::length(perpendicular) > 1e-6f))
    return false;
  directionOut = normalizedOrZero(perpendicular);
  left = glm::cross(upOut, directionOut);
  return true;
}

void Hdri::commitParameters()
{
  m_color = getParam<vec3>("color", vec3(1.f));
  m_up = getParam<vec3>("up", vec3(0.f, 0.f, 1.f));
  m_direction = getParam<vec3>("direction", vec3(1.f, 0.f, 0.f));
  const float scale = getParam<float>("scale", 1.f);
  m_scale = std::isfinite(scale) ? std::max(scale, 0.f) : 0.f;
  m_radiance = getParamObject<Array2D>("radiance");
  m_radianceSet = hasParam("radiance");
  m_visible = getParam<bool>("visible", true);
  const std::string layout = getParamString("layout", "equirectangular");
  if (layout != "equirectangular") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'layout' \"%s\" is not supported; using "
        "\"equirectangular\"",
        layout.c_str());
  }
}

void Hdri::finalize()
{
  gpu_enqueue_method(this, &Hdri::gpu_finalize);
}

bool Hdri::isValid() const
{
  return m_valid;
}

vec3 Hdri::up() const
{
  return m_up;
}

vec3 Hdri::hdriDirection() const
{
  return m_direction;
}

vec3 Hdri::emission() const
{
  return m_color * m_scale;
}

vec3 Hdri::meanRadiance() const
{
  return m_meanRadiance;
}

const GPUTexture &Hdri::texture() const
{
  return m_texture;
}

void Hdri::gpu_finalize()
{
  gpu_freeResources();

  vec3 left, up, direction;
  if (!frame(m_up, m_direction, left, up, direction)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'up' and 'direction' must be non-zero, finite and not "
        "parallel; the light is invalid");
    return;
  }
  if (!m_radiance) {
    reportMessage(ANARI_SEVERITY_WARNING,
        m_radianceSet
            ? "hdri light 'radiance' must be an ARRAY2D; the light is invalid"
            : "hdri light 'radiance' is missing; the light is invalid");
    return;
  }
  if (m_radiance->elementType() != ANARI_FLOAT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'radiance' must have FLOAT32_VEC3 elements (got %s); the "
        "light is invalid",
        anari::toString(m_radiance->elementType()));
    return;
  }
  const uint32_t width = uint32_t(m_radiance->size().x);
  const uint32_t height = uint32_t(m_radiance->size().y);
  if (width == 0 || height == 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'radiance' is empty; the light is invalid");
    return;
  }

  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  // The texels as RGBA32F, negative and non-finite values taken as 0, and
  // their mean over the sphere: row y spans the polar angles from
  // pi (H - 1 - y) / H to pi (H - y) / H measured from 'up'
  const auto *src = static_cast<const vec3 *>(m_radiance->data());
  bool clamped = false;
  double sum[3] = {0.0, 0.0, 0.0};
  m_texture = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
      SDL_GPU_TEXTUREUSAGE_SAMPLER);
  const bool uploaded =
      m_texture.uploadWith(uint32_t(sizeof(vec4)) * width * height,
          width,
          height,
          1,
          [&](void *dst) {
            auto *texels = static_cast<vec4 *>(dst);
            constexpr double pi = 3.14159265358979323846;
            for (uint32_t y = 0; y < height; y++) {
              // the row's solid angle over 4 pi, spread over its texels
              const double rowWeight = 0.5
                  * (std::cos(pi * double(y) / double(height))
                      - std::cos(pi * double(y + 1) / double(height)))
                  / double(width);
              for (uint32_t x = 0; x < width; x++) {
                const size_t i = size_t(y) * width + x;
                vec3 v = src[i];
                for (int c = 0; c < 3; c++) {
                  if (!(v[c] >= 0.f) || !std::isfinite(v[c])) {
                    v[c] = std::isfinite(v[c]) ? std::max(v[c], 0.f) : 0.f;
                    clamped = true;
                  }
                  sum[c] += rowWeight * v[c];
                }
                texels[i] = vec4(v, 1.f);
              }
            }
          });
  if (!uploaded) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create hdri light texture: %s",
        SDL_GetError());
    m_texture = {};
    return;
  }
  if (clamped) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'radiance' has negative or non-finite values; they are "
        "taken as 0");
  }

  m_meanRadiance = vec3(float(sum[0]), float(sum[1]), float(sum[2]));
  m_valid = true;
}

void Hdri::gpu_freeResources()
{
  m_valid = false;
  m_meanRadiance = vec3(0.f);
  m_texture = {};
}

} // namespace helide_gpu
