// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Orbit.h"
// std
#include <cmath>

namespace manipulators {

// Helper functions ///////////////////////////////////////////////////////////

static float degreesToRadians(float degrees)
{
  return degrees * M_PI / 180.f;
}

static anari::float3 azelToDirection(float az, float el, OrbitAxis axis)
{
  const float x = std::sin(az) * std::cos(el);
  const float y = std::cos(az) * std::cos(el);
  const float z = std::sin(el);
  switch (axis) {
  case OrbitAxis::POS_X:
    return -normalize(anari::float3(z, y, x));
  case OrbitAxis::POS_Y:
    return -normalize(anari::float3(x, z, y));
  case OrbitAxis::POS_Z:
    return -normalize(anari::float3(x, y, z));
  case OrbitAxis::NEG_X:
    return normalize(anari::float3(z, y, x));
  case OrbitAxis::NEG_Y:
    return normalize(anari::float3(x, z, y));
  case OrbitAxis::NEG_Z:
    return normalize(anari::float3(x, y, z));
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

Orbit::Orbit(anari::float3 at, float dist, anari::float2 azel)
{
  setConfig(at, dist, azel);
}

void Orbit::setConfig(anari::float3 at, float dist, anari::float2 azel)
{
  m_at = at;
  m_distance = dist;
  m_azel = azel;
  m_speed = dist;
  m_originalDistance = dist;
  update();
}

void Orbit::startNewRotation()
{
  m_invertRotation = m_azel.y > 90.f && m_azel.y < 270.f;
}

bool Orbit::hasChanged(UpdateToken &t) const
{
  if (t < m_token) {
    t = m_token;
    return true;
  } else
    return false;
}

void Orbit::rotate(anari::float2 delta)
{
  delta *= 100;
  delta.x = m_invertRotation ? -delta.x : delta.x;
  delta.y = m_distance < 0.f ? delta.y : -delta.y;
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

void Orbit::pan(anari::float2 delta)
{
  delta *= m_speed;

  const anari::float3 amount = delta.x * m_right + delta.y * m_up;

  m_eye += amount;
  m_at += amount;

  update();
}

void Orbit::setAxis(OrbitAxis axis)
{
  m_axis = axis;
  update();
}

anari::float2 Orbit::azel() const
{
  return m_azel;
}

anari::float3 Orbit::eye() const
{
  return m_eye;
}

anari::float3 Orbit::at() const
{
  return m_at;
}

anari::float3 Orbit::dir() const
{
  return linalg::normalize(at() - eye());
}

anari::float3 Orbit::up() const
{
  return m_up;
}

float Orbit::distance() const
{
  return m_distance;
}

anari::float3 Orbit::eye_FixedDistance() const
{
  return m_eyeFixedDistance;
}

void Orbit::update()
{
  const float distance = std::abs(m_distance);

  const OrbitAxis axis = m_distance < 0.f ? negateAxis(m_axis) : m_axis;

  const float azimuth = degreesToRadians(m_azel.x);
  const float elevation = degreesToRadians(m_azel.y);

  const anari::float3 toLocalOrbit = azelToDirection(azimuth, elevation, axis);

  const anari::float3 localOrbitPos = toLocalOrbit * distance;
  const anari::float3 fromLocalOrbit = -localOrbitPos;

  const anari::float3 alteredElevation =
      azelToDirection(azimuth, elevation + 3, m_axis);

  const anari::float3 cameraRight =
      linalg::cross(toLocalOrbit, alteredElevation);
  const anari::float3 cameraUp = linalg::cross(cameraRight, fromLocalOrbit);

  m_eye = localOrbitPos + m_at;
  m_up = linalg::normalize(cameraUp);
  m_right = linalg::normalize(cameraRight);

  m_eyeFixedDistance = (toLocalOrbit * m_originalDistance) + m_at;

  m_token++;
}

} // namespace manipulators
