// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp/ext/linalg.h>

namespace manipulators {

using UpdateToken = size_t;

enum class OrbitAxis
{
  POS_X,
  POS_Y,
  POS_Z,
  NEG_X,
  NEG_Y,
  NEG_Z
};

class Orbit
{
 public:
  Orbit() = default;
  Orbit(anari::math::float3 at, float dist, anari::math::float2 azel = anari::math::float2(0.f));

  void setConfig(
      anari::math::float3 at, float dist, anari::math::float2 azel = anari::math::float2(0.f));

  void startNewRotation();

  bool hasChanged(UpdateToken &t) const;

  void rotate(anari::math::float2 delta);
  void zoom(float delta);
  void pan(anari::math::float2 delta);

  void setAxis(OrbitAxis axis);

  anari::math::float2 azel() const;

  anari::math::float3 eye() const;
  anari::math::float3 at() const;
  anari::math::float3 dir() const;
  anari::math::float3 up() const;

  float distance() const;

  anari::math::float3 eye_FixedDistance() const; // using original distance

 protected:
  void update();

  // Data //

  UpdateToken m_token{1};

  // NOTE: degrees
  anari::math::float2 m_azel{0.f};

  float m_distance{1.f};
  float m_originalDistance{1.f};
  float m_speed{0.25f};

  bool m_invertRotation{false};

  anari::math::float3 m_eye;
  anari::math::float3 m_eyeFixedDistance;
  anari::math::float3 m_at;
  anari::math::float3 m_up;
  anari::math::float3 m_right;

  OrbitAxis m_axis{OrbitAxis::POS_Y};
};

} // namespace manipulators
