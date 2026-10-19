// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_volume.isph"

// The largest magnitude of the entries of 'values' (every 'stride'-th float
// from the first; n > 0 entries) that linear lookups (linearLookup()) at
// positions in [x0, x1] blend
static uniform float maxMagnitude(const uniform float *uniform values,
    uniform uint64 n,
    uniform int stride,
    uniform float x0,
    uniform float x1)
{
  const uniform int last = (uniform int)n - 1;
  const uniform int i0 = clamp((uniform int)floor(x0 * last), 0, last);
  const uniform int i1 = clamp((uniform int)ceil(x1 * last), 0, last);
  uniform float m = 0.f;
  for (uniform int i = i0; i <= i1; i++)
    m = max(m, abs(values[i * stride]));
  return m;
}

// The largest extinction the transfer function gives values in
// [lower, upper] (all of its range where those aren't known, lower > upper):
// from the largest opacity and colour alpha it blends there, so it bounds
// the extinction of every sample in that range
static uniform float majorantOver(
    const uniform Volume *uniform self, uniform float lower, uniform float upper)
{
  uniform float x0 = 0.f;
  uniform float x1 = 1.f;
  if (lower <= upper) {
    const uniform float a =
        (lower - self->valueRangeLower) * self->valueRangeInvSize;
    const uniform float b =
        (upper - self->valueRangeLower) * self->valueRangeInvSize;
    // NaN for an empty 'valueRange' (see Volume_normalized()): all of it
    if (!isnan(a) && !isnan(b)) {
      x0 = clamp(min(a, b), 0.f, 1.f);
      x1 = clamp(max(a, b), 0.f, 1.f);
    }
  }
  const uniform float opacity = self->opacities
      ? maxMagnitude(self->opacities, self->numOpacities, 1, x0, x1)
      : abs(self->opacity);
  const uniform float alpha = self->colors
      ? maxMagnitude(self->colors + 3, self->numColors, 4, x0, x1)
      : abs(self->color.w);
  return Volume_extinction(opacity * alpha, self->unitDistance);
}

// The volume's majorant: over the values the field takes
static uniform float majorant(const uniform Volume *uniform self)
{
  return self->field
      ? majorantOver(self, self->field->valueLower, self->field->valueUpper)
      : majorantOver(self, 1.f, 0.f);
}

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_Volume()
{
  return sizeof(uniform Volume);
}

// No field, the transfer function's parameter defaults, id ~0u
export void Volume_setDefaults(void *uniform _self)
{
  uniform Volume *uniform self = object_cast<Volume>(_self);
  self->field = NULL;
  self->valueRangeLower = 0.f;
  self->valueRangeInvSize = 1.f;
  self->unitDistance = 1.f;
  self->color = make_float4(1.f);
  self->colors = NULL;
  self->numColors = 0;
  self->opacity = 1.f;
  self->opacities = NULL;
  self->numOpacities = 0;
  self->id = 0xffffffffu;
  self->majorant = 0.f;
  self->macrocellMajorants = NULL;
  self->numMacrocells = 0;
}

// transferFunction1D parameters: 'field' (ISPC SpatialField, may be NULL),
// the value range, uniform 'color' (float4) and 'opacity', and the optional
// 'colors' (packed float4) and 'opacities' arrays with their sizes; computes
// the pathtracer's majorant from them and the field's value range, and drops
// the macrocell majorants (see Volume_setMacrocellMajorants())
export void Volume_setTransferFunction1D(void *uniform _self,
    void *uniform field,
    uniform float valueRangeLower,
    uniform float valueRangeUpper,
    uniform float unitDistance,
    const uniform float *uniform color,
    const uniform float *uniform colors,
    uniform uint64 numColors,
    uniform float opacity,
    const uniform float *uniform opacities,
    uniform uint64 numOpacities,
    uniform uint32 id)
{
  uniform Volume *uniform self = object_cast<Volume>(_self);
  self->field = object_cast<SpatialField>(field);
  self->valueRangeLower = valueRangeLower;
  // as helium's position(): may be inf/NaN for an empty range, like helide
  self->valueRangeInvSize = 1.f / (valueRangeUpper - valueRangeLower);
  self->unitDistance = unitDistance;
  self->color = load_float4(color);
  self->colors = numColors > 0 ? colors : NULL;
  self->numColors = numColors;
  self->opacity = opacity;
  self->opacities = numOpacities > 0 ? opacities : NULL;
  self->numOpacities = numOpacities;
  self->id = id;
  self->majorant = majorant(self);
  self->macrocellMajorants = NULL;
  self->numMacrocells = 0;
}

// Computes the majorants of the field's macrocells [begin, end) into
// 'majorants' (SpatialField_numMacrocells() of them) from the transfer
// function set by Volume_setTransferFunction1D() and the field's macrocell
// value ranges: each cell's from the values its samples take, 0 for cells
// of NaN only. Cells may be computed in parallel.
export void Volume_computeMacrocellMajorants(const void *uniform _self,
    uniform float *uniform majorants,
    uniform uint64 begin,
    uniform uint64 end)
{
  const uniform Volume *uniform self = (const uniform Volume *uniform)_self;
  const uniform float *uniform ranges =
      self->field ? self->field->macrocellRanges : NULL;
  for (uniform uint64 c = begin; c < end; c++) {
    if (!ranges) {
      majorants[c] = self->majorant;
      continue;
    }
    const uniform float lower = ranges[2 * c];
    const uniform float upper = ranges[2 * c + 1];
    majorants[c] = lower <= upper ? majorantOver(self, lower, upper) : 0.f;
  }
}

// Hands the volume its 'n' macrocell majorants (computed by
// Volume_computeMacrocellMajorants(), owned by the caller; NULL for none,
// where the volume's majorant holds everywhere)
export void Volume_setMacrocellMajorants(void *uniform _self,
    const uniform float *uniform majorants,
    uniform uint64 n)
{
  uniform Volume *uniform self = object_cast<Volume>(_self);
  self->macrocellMajorants = n > 0 ? majorants : NULL;
  self->numMacrocells = n > 0 && majorants ? n : 0;
}
