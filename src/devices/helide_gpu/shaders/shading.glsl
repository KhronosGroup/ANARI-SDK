// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Surface shading shared by triangle.frag and sphere.frag: the lighting model
// of OpenUSD Storm's UsdPreviewSurface (usdShaders' previewSurface.glslfx,
// metallic workflow, no clearcoat). Direct light is a Lambert diffuse plus a
// GGX specular lobe with Schlick's Fresnel; the uniform ambient light is
// Storm's dome-light term for a constant environment, with the split-sum BRDF
// table replaced by Karis' analytic fit ("Physically Based Shading on
// Mobile", 2014). All vectors are in view space.

#include "ambient_packing.glsl"

const float PI = 3.14159265;
const float EPSILON = 0.001;

// Light types (LightType in light/Light.h, plus the renderer's headlight)
const int LIGHT_DIRECTIONAL = 0;
const int LIGHT_POINT = 1;
const int LIGHT_SPOT = 2;
const int LIGHT_HEADLIGHT = 3;
const int LIGHT_QUAD = 4;

const int MAX_LIGHTS = 16;

struct LightData
{
  // xyz: view-space position (point, spot, the quad's center); w: the light
  // type
  vec4 position;
  // xyz: view-space emission direction (directional, spot, the normal of the
  // quad's emitting side); w: the cosine of the spot's half opening angle, or
  // 1 if both of the quad's sides emit
  vec4 direction;
  // rgb: color times irradiance (directional, headlight) or intensity (point,
  // spot, the quad along its normal); w: the cosine of the angle where the
  // spot's falloff starts
  vec4 radiance;
};

// Fragment uniform buffer slot 2: lighting, pushed once per frame
layout(set = 3, binding = 2) uniform LightingUniforms
{
  // rgb: the uniform ambient radiance
  vec4 ambient;
  int numLights;
  // 1: the ambient term is left out of the color, for the composite pass to
  // add it attenuated by ambient occlusion (packAmbient()); 0: it is added
  int deferAmbient;
  // 1 for a perspective camera, 0 for an orthographic one
  int perspective;
  int _pad;
  LightData lights[MAX_LIGHTS];
} light_u;

struct ShadingResult
{
  vec3 direct;
  vec3 ambient;
};

// The direction from the view-space point 'P' to the viewer
vec3 viewDirection(vec3 P)
{
  return light_u.perspective != 0 ? normalize(-P) : vec3(0.0, 0.0, 1.0);
}

// 'N' turned to the viewer's side of the surface whose geometric normal is
// 'Ng' (either orientation), so back faces are lit like front faces
vec3 faceForward(vec3 N, vec3 Ng, vec3 P)
{
  vec3 V = light_u.perspective != 0 ? -P : vec3(0.0, 0.0, 1.0);
  if (dot(Ng, V) < 0.0)
    Ng = -Ng;
  return dot(N, Ng) < 0.0 ? -N : N;
}

float schlickFresnel(float EdotH)
{
  return pow(max(0.0, 1.0 - EdotH), 5.0);
}

float normalDistribution(float roughness, float NdotH)
{
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
  return (alpha2 + EPSILON) / (PI * denom * denom);
}

float geometric(float roughness, float NdotL, float NdotE)
{
  float k = roughness * roughness * 0.5;
  return NdotE / (NdotE * (1.0 - k) + k) * NdotL / (NdotL * (1.0 - k) + k);
}

// Karis' fit of the split-sum environment BRDF: specular = F0 * x + y
vec2 envBRDFApprox(float NdotE, float roughness)
{
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 r = roughness * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NdotE)) * r.x + r.y;
  return vec2(-1.04, 1.04) * a004 + r.zw;
}

// Shades the view-space point 'P' with unit normal 'N' (on the viewer's side)
ShadingResult shade(vec3 P,
    vec3 N,
    vec3 baseColor,
    float metallic,
    float roughness,
    float ior)
{
  vec3 E = viewDirection(P);
  float NdotE = max(dot(N, E), 0.0);
  roughness = max(roughness, EPSILON);

  // Reflectance at normal (F0) and grazing (F90) incidence
  float R = (1.0 - ior) / (1.0 + ior);
  vec3 specColor = mix(vec3(1.0), baseColor, metallic);
  vec3 F0 = mix(R * R * specColor, specColor, metallic);
  vec3 F90 = specColor;
  // Pure metals have no diffuse
  vec3 diffuseColor = baseColor * (1.0 - metallic);

  ShadingResult result;
  result.direct = vec3(0.0);

  for (int i = 0; i < light_u.numLights; ++i) {
    LightData light = light_u.lights[i];
    int type = int(light.position.w);

    vec3 L;
    vec3 radiance = light.radiance.rgb;
    if (type == LIGHT_DIRECTIONAL) {
      L = -light.direction.xyz;
    } else if (type == LIGHT_HEADLIGHT) {
      L = E;
    } else {
      vec3 toLight = light.position.xyz - P;
      float dist2 = max(dot(toLight, toLight), EPSILON * EPSILON);
      L = toLight * inversesqrt(dist2);
      radiance /= dist2;
      if (type == LIGHT_SPOT) {
        radiance *= smoothstep(light.direction.w,
            light.radiance.w,
            dot(-L, light.direction.xyz));
      } else if (type == LIGHT_QUAD) {
        // Storm's point-light approximation of a rect light, plus the
        // Lambertian falloff of a flat emitter
        float cosEmit = dot(-L, light.direction.xyz);
        radiance *= light.direction.w != 0.0 ? abs(cosEmit) : max(cosEmit, 0.0);
      }
    }

    float NdotL = dot(N, L);
    if (NdotL <= 0.0)
      continue;

    vec3 H = normalize(E + L);
    float NdotH = max(dot(N, H), 0.0);
    float EdotH = max(dot(E, H), 0.0);
    vec3 F = mix(F0, F90, schlickFresnel(EdotH));

    vec3 specular = F * geometric(roughness, NdotL, NdotE)
        * normalDistribution(roughness, NdotH)
        / (4.0 * NdotL * NdotE + EPSILON);
    // Less diffuse at grazing angles, where more is reflected
    vec3 diffuse = diffuseColor * (1.0 - F) / PI;

    result.direct += NdotL * (diffuse + specular) * radiance;
  }

  // The uniform ambient light: Storm's dome light, whose irradiance and
  // prefiltered radiance are both the ambient radiance; seen from the
  // reflection direction, Fresnel is F0
  vec2 envBRDF = envBRDFApprox(NdotE, roughness);
  result.ambient = light_u.ambient.rgb
      * (diffuseColor * (1.0 - F0) + F0 * envBRDF.x + envBRDF.y);

  return result;
}
