#version 450

layout(location = 0) in vec3 vLocalPos; // interpolated [0,1]^3

layout(set = 2, binding = 0) uniform sampler3D volumeField;
layout(set = 2, binding = 1) uniform sampler2D transferFunc;

layout(set = 3, binding = 0) uniform VolumeParams
{
  vec4 localCamPos;    // camera position in [0,1]^3 local space
  vec4 fieldExtent;    // (dims-1)*spacing — scale from [0,1] to field space
  vec2 valueRange;     // (min, max)
  float localStepSize; // step size in [0,1] local units
  float oneOverUnitDistance;
  uint objectId;
  uint instanceId;
  float jitterSeed;    // for dithering the first sample
  float _pad;
} vp;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out uvec4 outIds;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outNormal;

// Simple hash for pseudo-random dithering
float hash(vec2 p)
{
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

// Ray-AABB intersection against [0,1]^3
// Returns (tNear, tFar); tNear > tFar means no intersection
vec2 intersectBox(vec3 origin, vec3 invDir)
{
  vec3 t0 = (vec3(0.0) - origin) * invDir;
  vec3 t1 = (vec3(1.0) - origin) * invDir;
  vec3 tMin = min(t0, t1);
  vec3 tMax = max(t0, t1);
  float tNear = max(max(tMin.x, tMin.y), tMin.z);
  float tFar = min(min(tMax.x, tMax.y), tMax.z);
  return vec2(tNear, tFar);
}

void main()
{
  vec3 rayOrigin = vp.localCamPos.xyz;
  vec3 rayDir = normalize(vLocalPos - rayOrigin);
  vec3 invDir = 1.0 / rayDir;

  vec2 tRange = intersectBox(rayOrigin, invDir);

  // Clamp entry to 0 (camera may be inside the volume)
  tRange.x = max(tRange.x, 0.0);

  if (tRange.x >= tRange.y) {
    discard;
  }

  float dt = vp.localStepSize;
  if (dt <= 0.0) {
    discard;
  }

  // Dither the first sample to reduce banding
  float jitter = hash(gl_FragCoord.xy + vec2(vp.jitterSeed));
  float t = tRange.x + jitter * dt;

  vec3 accColor = vec3(0.0);
  float accAlpha = 0.0;
  bool hitRecorded = false;

  float rangeInv = 1.0 / max(vp.valueRange.y - vp.valueRange.x, 1e-10);

  // Convert step from [0,1] space to field-space distance for opacity correction.
  // rayDir is normalized in [0,1] space; multiplying by extent gives field-space
  // direction.  The length of that times dt gives the actual field distance per step.
  float fieldDistPerStep = length(rayDir * vp.fieldExtent.xyz) * dt;
  float stepExponent = fieldDistPerStep * vp.oneOverUnitDistance;

  while (t < tRange.y && accAlpha < 0.99) {
    vec3 pos = rayOrigin + rayDir * t;

    // Sample the scalar field (R32F texture, normalized coords)
    float scalar = texture(volumeField, pos).r;

    // Map to transfer function coordinate [0, 1]
    float tc = clamp((scalar - vp.valueRange.x) * rangeInv, 0.0, 1.0);

    // Look up transfer function (256x1 2D texture)
    vec4 tfSample = texture(transferFunc, vec2(tc, 0.5));

    // Opacity correction for step size
    float correctedAlpha = 1.0 - pow(1.0 - tfSample.a, stepExponent);

    if (correctedAlpha > 0.0) {
      float weight = 1.0 - accAlpha;
      accColor += weight * correctedAlpha * tfSample.rgb;
      accAlpha += weight * correctedAlpha;

      if (!hitRecorded && accAlpha > 0.01) {
        hitRecorded = true;
      }
    }

    t += dt;
  }

  if (accAlpha < 1e-6) {
    discard;
  }

  fragColor = vec4(accColor, accAlpha);
  outIds = hitRecorded ? uvec4(0u, vp.objectId, vp.instanceId, 0u) : uvec4(~0u);
  outAlbedo = vec4(accColor, 1.0);
  outNormal = vec4(0.0);
}
