// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Ray marches volumes set up through volume's exported interface for
// test_volume.cpp.

#include "shading/halcyon_volume.isph"

// Volume_render() of 'volume' along 'n' rays given as packed float3 object
// space 'orgs'/'dirs' and intervals 'tLower'/'tUpper'; writes packed float4
// (color, opacity) 'results'
export void test_render(void *uniform volume,
    const uniform float *uniform orgs,
    const uniform float *uniform dirs,
    const uniform float *uniform tLower,
    const uniform float *uniform tUpper,
    uniform float invSamplingRate,
    uniform float *uniform results,
    uniform int n)
{
  uniform Volume *uniform vol = (uniform Volume * uniform) volume;
  foreach (i = 0 ... n) {
    VolumeHit vh;
    vh.volume = vol;
    vh.instance = NULL;
    vh.instArrayId = 0;
    vh.tLower = tLower[i];
    vh.tUpper = tUpper[i];
    vh.org = load_float3(orgs + 3 * i);
    vh.dir = load_float3(dirs + 3 * i);

    float3 color;
    float opacity;
    renderVolumeHit(vh, invSamplingRate, color, opacity);
    store_float4(results + 4 * i, make_float4(color, opacity));
  }
}

// Volume_jitter() at 'n' entry distances 't'
export void test_jitter(const uniform float *uniform t,
    uniform float stepSize,
    uniform float *uniform results,
    uniform int n)
{
  foreach (i = 0 ... n)
    results[i] = Volume_jitter(t[i], stepSize);
}

// The pathtracer's majorant of 'volume', and in 'maxExtinction' the largest
// extinction (Volume_extinction()) the volume has at 'n' packed float3
// object-space 'points' (NaN samples aside)
export uniform float test_majorant(void *uniform volume,
    const uniform float *uniform points,
    uniform int n,
    uniform float *uniform maxExtinction)
{
  const uniform Volume *uniform vol = (const uniform Volume *uniform)volume;
  float m = 0.f;
  foreach (i = 0 ... n) {
    const float s = SpatialField_sampleAt(vol->field, load_float3(points + 3 * i));
    if (!isnan(s)) {
      const float sigma = Volume_extinction(
          Volume_opacityOf(vol, s) * Volume_colorOf(vol, s).w,
          vol->unitDistance);
      m = max(m, sigma);
    }
  }
  *maxExtinction = reduce_max(m);
  return vol->majorant;
}

// Volume_majorantFrom() of 'volume' along 'n' object-space rays (packed
// float3 'orgs' and 'dirs') from distances 'ts': the majorants and the
// distances they hold until ('exits')
export void test_macrocellMajorant(void *uniform volume,
    const uniform float *uniform orgs,
    const uniform float *uniform dirs,
    const uniform float *uniform ts,
    uniform float *uniform majorants,
    uniform float *uniform exits,
    uniform int n)
{
  const uniform Volume *uniform vol = (const uniform Volume *uniform)volume;
  foreach (i = 0 ... n) {
    float exit;
    majorants[i] = Volume_majorantFrom(
        vol, load_float3(orgs + 3 * i), load_float3(dirs + 3 * i), ts[i], exit);
    exits[i] = exit;
  }
}

// The extinction (Volume_extinction()) of 'volume' at 'n' packed float3
// object-space 'points', 0 where the field is NaN
export void test_extinction(void *uniform volume,
    const uniform float *uniform points,
    uniform float *uniform results,
    uniform int n)
{
  const uniform Volume *uniform vol = (const uniform Volume *uniform)volume;
  foreach (i = 0 ... n) {
    const float s = SpatialField_sampleAt(vol->field, load_float3(points + 3 * i));
    results[i] = isnan(s) ? 0.f
                          : Volume_extinction(
                              Volume_opacityOf(vol, s) * Volume_colorOf(vol, s).w,
                              vol->unitDistance);
  }
}
