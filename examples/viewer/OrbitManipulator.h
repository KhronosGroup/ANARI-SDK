// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "glm_box3.h"

namespace manipulators {

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
  Orbit(const glm_box3 &worldBounds, glm::vec2 azel = glm::vec2(0.f));

  void startNewRotation();

  void rotate(glm::vec2 delta);
  void zoom(float delta);
  void pan(glm::vec2 delta);

  void setAxis(OrbitAxis axis);

  glm::vec3 eye() const;
  glm::vec3 dir() const;
  glm::vec3 up() const;

 protected:
  void update();

  // Data //

  // NOTE: degrees
  glm::vec2 m_azel{0.f};

  float m_distance{1.f};
  float m_worldSize{1.f};
  float m_speed{0.25f};

  bool m_invertRotation{false};

  glm::vec3 m_eye;
  glm::vec3 m_at;
  glm::vec3 m_up;
  glm::vec3 m_right;

  OrbitAxis m_axis{OrbitAxis::POS_Y};
};

} // namespace manipulators

using ArcballCamera = manipulators::Orbit;
