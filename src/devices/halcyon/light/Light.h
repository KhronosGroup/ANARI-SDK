// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array1D.h"
#include "array/Array2D.h"
// std
#include <cmath>
#include <vector>

namespace halcyon {

// Lights (KHR_LIGHT_DIRECTIONAL, KHR_LIGHT_POINT, KHR_LIGHT_SPOT,
// KHR_LIGHT_QUAD, KHR_LIGHT_RING, KHR_LIGHT_HDRI): only the 'quality' renderer is
// lit by them; the 'preview' renderer isn't, as helide's isn't, but shows the
// 'visible' hdris where its camera rays miss. Unknown subtypes are
// UnknownObjects.
//
// A light hands the world its record in world space (struct Light in
// shading/halcyon_objects.isph), once per instance transform: the transform
// moves 'position', turns 'direction', 'up' (hdri) and 'c0' and maps the
// quad's 'edge1' and 'edge2' (vectors), but doesn't scale 'radius' or 'innerRadius' (the
// ANARI spec: instance transforms apply to vectors, not to sizes).
//
// ANARI's brightness parameters are radiometric; each becomes the one
// quantity the light's shape is sampled with (Light::emission in ISPC, times
// 'color'). The first parameter the app set explicitly wins, in the spec's
// order; with none set, the first one's default (1) applies. r is the point
// light's 'radius'; a = 'angularDiameter' / 2, the cone's half angle,
// clamped to [0, pi]. A is the area of a quad (|edge1 x edge2| after the
// instance transform) or ring (pi (radius^2 - innerRadius^2)).
//
//   light        parameter    shape            emission
//   -----------  -----------  ---------------  ------------------------------
//   directional  irradiance   a = 0: delta     irradiance E (W/m^2)
//                E (W/m^2)    a > 0: cone      radiance E / (pi sin^2 a')
//                                              with a' = min(a, pi / 2)
//                radiance     a = 0            none (no light; warns)
//                L            a > 0: cone      radiance L (W/sr/m^2)
//   point        intensity    r = 0: delta     intensity I (W/sr)
//                I (W/sr)     r > 0: sphere    radiance I / (pi r^2)
//                power        as intensity     with I = P / (4 pi)
//                P (W)
//                radiance     r = 0            none (no light; warns)
//                L            r > 0: sphere    radiance L (W/sr/m^2)
//   spot         intensity    delta            intensity I along 'direction'
//                I (W/sr)
//                power P (W)  delta            with I = P / W
//   quad, ring   intensity    area A           radiance I / A
//                I (W/sr)     ring r = 0:      intensity I along the axis
//                             delta
//                power P (W)  as intensity     with I = P / W
//                radiance     area A           radiance L
//                L            ring r = 0       none (no light; warns)
//   hdri         radiance     the sphere at    radiance 'radiance' texel
//                (ARRAY2D)    infinity         times 'scale' (light/
//                                              Hdri.h has the mapping)
//
// The intensity 'I' is the light's along its axis (a spot's or ring's
// 'direction', a quad's emitting normal); in other directions it is I times
// the light's profile f: a spot's cone weight s (1 inside the core cone
// 'openingAngle' / 2 - 'falloffAngle', 0 outside 'openingAngle' / 2, a
// smoothstep in the angle between: Typhoon's shaping:cone), times for a
// quad or ring cos(gamma) (a Lambertian emitter; gamma the angle off the
// axis) or 'intensityDistribution' D(gamma, C) where that is set. So a quad
// or ring of 'intensityDistribution' D emits radiance I D s / (A cos gamma).
// W is the integral of f over the directions the light emits to: the
// power of the light per unit of I. For a spot the spec defines it as the
// cosine-weighted integral of s over its cone (about pi sin^2 of half the
// opening angle), not the integral of s; halcyon follows the spec, taking
// the cosine as 0 behind the light. A quad's back emits as its front does,
// so W doubles with 'side' "both".
//
// So 'irradiance' is what a surface facing the light receives whatever its
// angular diameter (pi sin^2 a' is the cosine-weighted solid angle of the
// cone, or of the hemisphere where the cone covers it), 'intensity' what a
// light radiates along its axis whatever its size (pi r^2 is a sphere's
// projected area), and 'power' what it radiates in all. Typhoon's UsdLux
// semantics (intensity times 2^exposure, 'normalize') are not used; its
// sampling is (renderer/pathtracer/Lights.isph).
struct Light : public Object
{
  Light(HalcyonDeviceGlobalState *d);
  ~Light() override = default;
  static Light *createInstance(
      std::string_view subtype, HalcyonDeviceGlobalState *d);

  // Writes the light's world-space record under the instance transform 'xfm'
  // into 'record' (sizeof_Light() bytes); false (nothing written) if the
  // transformed light emits nothing
  virtual bool writeWorldRecord(void *record, const mat4 &xfm) const = 0;

 protected:
  enum class Brightness
  {
    INTENSITY,
    POWER,
    RADIANCE
  };

  // 'color' (clamped to at least 0) and 'visible'
  void commitCommonParameters();
  // The first of 'intensity', 'power' and (if 'radiance') 'radiance' set as
  // a FLOAT32, or 'intensity' 1 if none is, into m_brightness and
  // m_brightnessValue (clamped to at least 0)
  void commitBrightness(bool radiance);
  // 'openingAngle' (pi, clamped to [0, 2 pi]) and 'falloffAngle' (0.1,
  // clamped to [0, openingAngle / 2]) as Typhoon's cone angles
  void commitCone();
  // 'intensityDistribution' (an ARRAY1D or ARRAY2D of FLOAT32), observed;
  // finalizeDistribution() copies it
  void commitDistribution();
  // Copies 'intensityDistribution' into m_distribution, warning and
  // dropping it if it isn't usable
  void finalizeDistribution();

  // Whether every component of 'v' is finite
  static bool isFinite(const float3 &v);
  // 'v' made unit length; zero if it is zero or not finite
  static float3 normalizedOrZero(const float3 &v);

  // Light.h's cone weight s at 'theta' radians off the axis (Typhoon's
  // 1 - smoothstep(theta, coneSoftAngle, coneAngle))
  float coneWeight(float theta) const;
  // 'intensityDistribution' at 'gamma' radians off the axis, averaged over
  // the C half-planes (the integral over C of its interpolation, / 2 pi)
  float distributionMeanOverC(float gamma) const;
  // 2 pi times the integral of profile(gamma) sin(gamma) over gamma in
  // [gamma0, gamma1]: the integral of a profile symmetric about the axis
  // over the solid angle between
  template <typename F>
  static float integrateOverSolidAngle(F &&profile, float gamma0, float gamma1);

  float3 m_color{1.f};
  bool m_visible{true};

  Brightness m_brightness{Brightness::INTENSITY};
  float m_brightnessValue{1.f};

  float m_coneAngle{0.f};
  float m_coneSoftAngle{0.f};

  helium::ChangeObserverPtr<Array1D> m_distribution1D{this};
  helium::ChangeObserverPtr<Array2D> m_distribution2D{this};
  // the copied 'intensityDistribution': numGamma values per C half-plane,
  // numC half-planes; empty if not set or not usable
  std::vector<float> m_distribution;
  uint32_t m_distributionNumGamma{0};
  uint32_t m_distributionNumC{0};
};

template <typename F>
inline float Light::integrateOverSolidAngle(
    F &&profile, float gamma0, float gamma1)
{
  // the midpoint rule: the profiles are piecewise smooth with a few kinks,
  // so 4096 steps get them within about 1e-6
  constexpr int STEPS = 4096;
  const double h = (double(gamma1) - double(gamma0)) / STEPS;
  double sum = 0.0;
  for (int i = 0; i < STEPS; i++) {
    const double gamma = gamma0 + (i + 0.5) * h;
    sum += double(profile(float(gamma))) * std::sin(gamma);
  }
  return float(2.0 * M_PI * sum * h);
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Light *, ANARI_LIGHT);
