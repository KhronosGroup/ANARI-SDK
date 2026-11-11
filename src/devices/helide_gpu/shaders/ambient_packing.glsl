// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The surfaces' ambient light, packed into the otherwise unused .w of the
// RGBA32_UINT ID target for the composite pass to add after ambient occlusion:
// fixed point over [0, AMBIENT_PACKING_RANGE] with 11, 11 and 10 bits. The
// ID target's clear value, all ones, unpacks to no ambient light (red stops
// at 2046, so no packed value is all ones).

const float AMBIENT_PACKING_RANGE = 4.0;

uint packAmbient(vec3 ambient)
{
  vec3 a = clamp(ambient / AMBIENT_PACKING_RANGE, 0.0, 1.0);
  uvec3 q = uvec3(round(a * vec3(2046.0, 2047.0, 1023.0)));
  return q.r | (q.g << 11) | (q.b << 22);
}

vec3 unpackAmbient(uint packed)
{
  if (packed == 0xFFFFFFFFu)
    return vec3(0.0);
  uvec3 q = uvec3(packed & 0x7FFu, (packed >> 11) & 0x7FFu, packed >> 22);
  return vec3(q) / vec3(2046.0, 2047.0, 1023.0) * AMBIENT_PACKING_RANGE;
}
