// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
// std
#include <algorithm>
#include <cmath>
#include <string>

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
  else if (subtype == "quad")
    return new Light(s, LightType::QUAD);
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

  m_visible = getParam<bool>("visible", true);
  m_radianceTimesArea = false;
  m_emitsFront = true;
  m_emitsBack = false;
  if (m_type == LightType::QUAD) {
    commitQuad(color);
    return;
  }

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

void Light::commitQuad(const vec3 &color)
{
  m_edge1 = getParam<vec3>("edge1", vec3(1.f, 0.f, 0.f));
  m_edge2 = getParam<vec3>("edge2", vec3(0.f, 1.f, 0.f));
  const vec3 origin = getParam<vec3>("position", vec3(0.f));
  m_position = origin + 0.5f * (m_edge1 + m_edge2);

  const std::string side = getParamString("side", "front");
  if (side != "front" && side != "back" && side != "both") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "quad light 'side' '%s' is not 'front', 'back' or 'both'; 'front' "
        "is used",
        side.c_str());
  }
  m_emitsBack = side == "back" || side == "both";
  m_emitsFront = side != "back";
  const vec3 normal = glm::cross(m_edge2, m_edge1);
  const float area = glm::length(normal);
  m_direction = area > 0.f ? (m_emitsFront ? normal : -normal) / area
                           : vec3(0.f, 0.f, -1.f);

  // The spec's precedence: the first of 'intensity' (W/sr along the normal),
  // 'power' (W) and 'radiance' (W/sr/m^2) set. A Lambertian side emits pi
  // times its intensity along the normal.
  float amount = 1.f;
  float value = 0.f;
  if (getParam("intensity", ANARI_FLOAT32, &value))
    amount = value;
  else if (getParam("power", ANARI_FLOAT32, &value))
    amount = value / (PI * (m_emitsFront && m_emitsBack ? 2.f : 1.f));
  else if (getParam("radiance", ANARI_FLOAT32, &value)) {
    amount = value;
    m_radianceTimesArea = true;
  }
  m_radiance = color * (std::isfinite(amount) ? std::max(amount, 0.f) : 0.f);
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

bool Light::radianceTimesArea() const
{
  return m_radianceTimesArea;
}

vec3 Light::edge1() const
{
  return m_edge1;
}

vec3 Light::edge2() const
{
  return m_edge2;
}

bool Light::emitsFront() const
{
  return m_emitsFront;
}

bool Light::emitsBack() const
{
  return m_emitsBack;
}

bool Light::visible() const
{
  return m_visible;
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Light *);
