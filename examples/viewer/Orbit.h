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
  Orbit(anari::float3 at, float dist, anari::float2 azel = anari::float2(0.f));

  void setConfig(
      anari::float3 at, float dist, anari::float2 azel = anari::float2(0.f));

  void startNewRotation();

  bool hasChanged(UpdateToken &t) const;

  void rotate(anari::float2 delta);
  void zoom(float delta);
  void pan(anari::float2 delta);

  void setAxis(OrbitAxis axis);

  anari::float2 azel() const;

  anari::float3 eye() const;
  anari::float3 at() const;
  anari::float3 dir() const;
  anari::float3 up() const;

  float distance() const;

  anari::float3 eye_FixedDistance() const; // using original distance

 protected:
  void update();

  // Data //

  UpdateToken m_token{1};

  // NOTE: degrees
  anari::float2 m_azel{0.f};

  float m_distance{1.f};
  float m_originalDistance{1.f};
  float m_speed{0.25f};

  bool m_invertRotation{false};

  anari::float3 m_eye;
  anari::float3 m_eyeFixedDistance;
  anari::float3 m_at;
  anari::float3 m_up;
  anari::float3 m_right;

  OrbitAxis m_axis{OrbitAxis::POS_Y};
};

} // namespace manipulators
