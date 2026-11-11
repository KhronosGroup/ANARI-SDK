// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace helide_gpu {

// The light types the surface shaders know (shaders/shading.glsl LIGHT_*)
enum class LightType : int32_t
{
  DIRECTIONAL = 0,
  POINT = 1,
  SPOT = 2,
  QUAD = 4, // 3 is the renderer's headlight
};

// A directional, point, spot or quad light (KHR_LIGHT_DIRECTIONAL, _POINT,
// _SPOT, _QUAD). A quad is shaded like Storm shades a rect light: as a point
// light at its center, with a Lambertian (cosine) falloff away from its
// normal.
struct Light : public Object
{
  Light(HelideGPUDeviceGlobalState *d, LightType type);
  ~Light() override = default;
  static Light *createInstance(
      std::string_view subtype, HelideGPUDeviceGlobalState *d);

  void commitParameters() override;

  LightType type() const;
  // Light-local position and emission direction (normalized); a quad's
  // position is its center and its direction the normal of its emitting side
  // (the front, cross(edge2, edge1), unless only the back emits)
  vec3 position() const;
  vec3 direction() const;
  // color times irradiance (directional) or intensity (point, spot, and a
  // quad along its normal); for a quad with 'radianceTimesArea()', multiply
  // by the quad's area in the frame's space
  vec3 radiance() const;
  bool radianceTimesArea() const;
  // A quad's light-local edges, and which of its sides emit (the front faces
  // along cross(edge2, edge1))
  vec3 edge1() const;
  vec3 edge2() const;
  bool emitsFront() const;
  bool emitsBack() const;
  // Whether the camera sees the light (KHR_LIGHT_PRIMARY_VISIBILITY); only a
  // quad has an area to see
  bool visible() const;
  // Spot cone: cosines of the half opening angle and of where the falloff
  // starts
  float cosOuterAngle() const;
  float cosInnerAngle() const;

 private:
  void commitQuad(const vec3 &color);

  LightType m_type{LightType::DIRECTIONAL};
  vec3 m_position{0.f};
  vec3 m_direction{0.f, 0.f, -1.f};
  vec3 m_radiance{1.f};
  float m_cosOuterAngle{-1.f};
  float m_cosInnerAngle{-1.f};
  vec3 m_edge1{1.f, 0.f, 0.f};
  vec3 m_edge2{0.f, 1.f, 0.f};
  bool m_radianceTimesArea{false};
  bool m_emitsFront{true};
  bool m_emitsBack{false};
  bool m_visible{true};
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Light *, ANARI_LIGHT);
