// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "OrbitManipulator.h"

namespace manipulators {

// Helper functions ///////////////////////////////////////////////////////////

static float degreesToRadians(float degrees)
{
  return degrees * M_PI / 180.f;
}

static glm::vec3 azelToDirection(float az, float el, OrbitAxis axis)
{
  const float x = std::sin(az) * std::cos(el);
  const float y = std::cos(az) * std::cos(el);
  const float z = std::sin(el);
  switch (axis) {
  case OrbitAxis::POS_X:
    return -normalize(glm::vec3(z, y, x));
  case OrbitAxis::POS_Y:
    return -normalize(glm::vec3(x, z, y));
  case OrbitAxis::POS_Z:
    return -normalize(glm::vec3(x, y, z));
  case OrbitAxis::NEG_X:
    return normalize(glm::vec3(z, y, x));
  case OrbitAxis::NEG_Y:
    return normalize(glm::vec3(x, z, y));
  case OrbitAxis::NEG_Z:
    return normalize(glm::vec3(x, y, z));
  }
  return {};
}

static OrbitAxis negateAxis(OrbitAxis current)
{
  switch (current) {
  case OrbitAxis::POS_X:
    return OrbitAxis::NEG_X;
  case OrbitAxis::POS_Y:
    return OrbitAxis::NEG_Y;
  case OrbitAxis::POS_Z:
    return OrbitAxis::NEG_Z;
  case OrbitAxis::NEG_X:
    return OrbitAxis::POS_X;
  case OrbitAxis::NEG_Y:
    return OrbitAxis::POS_Y;
  case OrbitAxis::NEG_Z:
    return OrbitAxis::POS_Z;
  }
  return {};
}

static float maintainUnitCircle(float inDegrees)
{
  while (inDegrees > 360.f)
    inDegrees -= 360.f;
  while (inDegrees < 0.f)
    inDegrees += 360.f;
  return inDegrees;
}

// Orbit definitions //////////////////////////////////////////////////////////

Orbit::Orbit(const glm_box3 &worldBounds, glm::vec2 azel) : m_azel(azel)
{
  m_at = worldBounds.center();
  m_distance = glm::length(worldBounds.size()) * 0.8f;
  m_speed = m_distance;
  update();
}

void Orbit::startNewRotation()
{
  m_invertRotation = m_azel.y > 90.f && m_azel.y < 270.f;
}

void Orbit::rotate(glm::vec2 delta)
{
  delta *= 100;
  delta.x = m_invertRotation ? -delta.x : delta.x;
  delta.y = m_distance < 0.f ? -delta.y : delta.y;
  m_azel += delta;
  m_azel.x = maintainUnitCircle(m_azel.x);
  m_azel.y = maintainUnitCircle(m_azel.y);
  update();
}

void Orbit::zoom(float delta)
{
  m_distance += m_speed * delta;
  update();
}

void Orbit::pan(glm::vec2 delta)
{
  delta *= m_speed;
  delta.y = -delta.y;

  const glm::vec3 amount = delta.x * m_right + delta.y * m_up;

  m_eye += amount;
  m_at += amount;

  update();
}

void Orbit::setAxis(OrbitAxis axis)
{
  m_axis = axis;
  update();
}

glm::vec3 Orbit::eye() const
{
  return m_eye;
}

glm::vec3 Orbit::dir() const
{
  return glm::normalize(m_at - m_eye);
}

glm::vec3 Orbit::up() const
{
  return m_up;
}

void Orbit::update()
{
  const float distance = std::abs(m_distance);

  const OrbitAxis axis = m_distance < 0.f ? negateAxis(m_axis) : m_axis;

  const float azimuth = degreesToRadians(m_azel.x);
  const float elevation = degreesToRadians(m_azel.y);

  const glm::vec3 toLocalOrbit = azelToDirection(azimuth, elevation, axis);

  const glm::vec3 localOrbitPos = toLocalOrbit * distance;
  const glm::vec3 fromLocalOrbit = -localOrbitPos;

  const glm::vec3 alteredElevation =
      azelToDirection(azimuth, elevation + 3, m_axis);

  const glm::vec3 cameraRight = glm::cross(toLocalOrbit, alteredElevation);
  const glm::vec3 cameraUp = glm::cross(cameraRight, fromLocalOrbit);

  m_eye = localOrbitPos + m_at;
  m_up = glm::normalize(cameraUp);
  m_right = glm::normalize(cameraRight);
}

} // namespace manipulators
