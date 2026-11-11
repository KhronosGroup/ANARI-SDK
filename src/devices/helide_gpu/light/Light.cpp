// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
// std
#include <algorithm>
#include <cmath>

namespace helide_gpu {

constexpr float PI = 3.14159265358979f;

Light::Light(HelideGPUDeviceGlobalState *s, LightType type)
    : Object(ANARI_LIGHT, s), m_type(type)
{}

Light *Light::createInstance(
    std::string_view subtype, HelideGPUDeviceGlobalState *s)
{
  if (subtype == "directional")
    return new Light(s, LightType::DIRECTIONAL);
  else if (subtype == "point")
    return new Light(s, LightType::POINT);
  else if (subtype == "spot")
    return new Light(s, LightType::SPOT);
  return (Light *)new UnknownObject(ANARI_LIGHT, s);
}

void Light::commitParameters()
{
  const vec3 color = getParam<vec3>("color", vec3(1.f));
  m_position = getParam<vec3>("position", vec3(0.f));
  m_direction = getParam<vec3>("direction", vec3(0.f, 0.f, -1.f));
  const float len = glm::length(m_direction);
  m_direction = len > 0.f ? m_direction / len : vec3(0.f, 0.f, -1.f);

  // the spot's half opening angle, and where its falloff toward the edge
  // starts; the falloff is at most the whole cone
  const float halfOpening =
      std::clamp(getParam<float>("openingAngle", PI), 0.f, 2.f * PI) * 0.5f;
  const float falloff =
      std::clamp(getParam<float>("falloffAngle", 0.1f), 0.f, halfOpening);
  m_cosOuterAngle = std::cos(halfOpening);
  m_cosInnerAngle = std::cos(halfOpening - falloff);

  float amount = 1.f;
  if (m_type == LightType::DIRECTIONAL)
    amount = getParam<float>("irradiance", 1.f);
  else {
    // 'power' (W) takes precedence over 'intensity' (W/sr): spread over the
    // solid angle of the sphere, or of the spot's cone
    float power = 0.f;
    if (getParam("power", ANARI_FLOAT32, &power)) {
      const float solidAngle = m_type == LightType::SPOT
          ? 2.f * PI * (1.f - m_cosOuterAngle)
          : 4.f * PI;
      amount = solidAngle > 0.f ? power / solidAngle : 0.f;
    } else
      amount = getParam<float>("intensity", 1.f);
  }
  m_radiance = color * std::max(amount, 0.f);
}

LightType Light::type() const
{
  return m_type;
}

vec3 Light::position() const
{
  return m_position;
}

vec3 Light::direction() const
{
  return m_direction;
}

vec3 Light::radiance() const
{
  return m_radiance;
}

float Light::cosOuterAngle() const
{
  return m_cosOuterAngle;
}

float Light::cosInnerAngle() const
{
  return m_cosInnerAngle;
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Light *);
