// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_spatialField.isph"

inline float nan()
{
  return floatbits(0x7fc00000);
}

// structuredRegular (helide's StructuredRegularField::sampleAt()) ////////////

// Voxel at 'i' converted to float; normalized types are divided by their
// maximum like helide does
inline float voxel_float(const uniform StructuredRegularField &f, int64 i)
{
#pragma ignore warning(perf)
  return ((const uniform float *uniform)f.voxels)[i];
}

inline float voxel_double(const uniform StructuredRegularField &f, int64 i)
{
#pragma ignore warning(perf)
  return (float)((const uniform double *uniform)f.voxels)[i];
}

inline float voxel_uint8(const uniform StructuredRegularField &f, int64 i)
{
#pragma ignore warning(perf)
  const uint8 v = ((const uniform uint8 *uniform)f.voxels)[i];
  return (float)v / 255.f;
}

inline float voxel_uint16(const uniform StructuredRegularField &f, int64 i)
{
#pragma ignore warning(perf)
  const uint16 v = ((const uniform uint16 *uniform)f.voxels)[i];
  return (float)v / 65535.f;
}

inline float voxel_int16(const uniform StructuredRegularField &f, int64 i)
{
#pragma ignore warning(perf)
  const int16 v = ((const uniform int16 *uniform)f.voxels)[i];
  return (float)v / 32767.f;
}

// Trilinear sample hook for voxels of type T: NaN outside
// [origin, origin + (dims - 1) * spacing], blended along x, then y, then z
#define STRUCTURED_REGULAR_SAMPLE(T)                                           \
  static varying float StructuredRegular_sample_##T(                           \
      const uniform SpatialField *uniform self, const varying float3 &p)       \
  {                                                                            \
    const uniform StructuredRegularField &f = self->structuredRegular;         \
    const float3 local = f.invSpacing * (p - f.origin);                        \
                                                                               \
    /* written so that NaN coordinates are outside too */                      \
    if (!(local.x >= 0.f && local.x <= f.dims[0] - 1.f && local.y >= 0.f       \
            && local.y <= f.dims[1] - 1.f && local.z >= 0.f                    \
            && local.z <= f.dims[2] - 1.f))                                    \
      return nan();                                                            \
                                                                               \
    const int x0 = (int)local.x;                                               \
    const int y0 = (int)local.y;                                               \
    const int z0 = (int)local.z;                                               \
    const int x1 = min(x0 + 1, f.dims[0] - 1);                                 \
    const int y1 = min(y0 + 1, f.dims[1] - 1);                                 \
    const int z1 = min(z0 + 1, f.dims[2] - 1);                                 \
    const float fx = local.x - x0;                                             \
    const float fy = local.y - y0;                                             \
    const float fz = local.z - z0;                                             \
                                                                               \
    /* row offsets of (y, z) corners; v<y><z> are blended along x */          \
    const int64 sx = f.dims[0];                                                \
    const int64 sxy = sx * f.dims[1];                                          \
    const int64 i00 = y0 * sx + z0 * sxy;                                      \
    const int64 i01 = y1 * sx + z0 * sxy;                                      \
    const int64 i10 = y0 * sx + z1 * sxy;                                      \
    const int64 i11 = y1 * sx + z1 * sxy;                                      \
                                                                               \
    const float v00 = lerp(voxel_##T(f, i00 + x0), voxel_##T(f, i00 + x1), fx); \
    const float v01 = lerp(voxel_##T(f, i01 + x0), voxel_##T(f, i01 + x1), fx); \
    const float v10 = lerp(voxel_##T(f, i10 + x0), voxel_##T(f, i10 + x1), fx); \
    const float v11 = lerp(voxel_##T(f, i11 + x0), voxel_##T(f, i11 + x1), fx); \
    const float v0 = lerp(v00, v01, fy);                                       \
    const float v1 = lerp(v10, v11, fy);                                       \
    return lerp(v0, v1, fz);                                                   \
  }

STRUCTURED_REGULAR_SAMPLE(float)
STRUCTURED_REGULAR_SAMPLE(double)
STRUCTURED_REGULAR_SAMPLE(uint8)
STRUCTURED_REGULAR_SAMPLE(uint16)
STRUCTURED_REGULAR_SAMPLE(int16)

#undef STRUCTURED_REGULAR_SAMPLE

// Macrocells //////////////////////////////////////////////////////////////////

// Voxel intervals along each edge of a structuredRegular field's macrocells
#define STRUCTURED_REGULAR_MACROCELL_VOXELS 8

// The value ranges of the field's macrocells [begin, end) for voxels of
// type T, into 'ranges' (packed float2, indexed by cell): the smallest and
// largest of the voxels that samples in the cell blend, as the sample hooks
// convert them (NaN voxels skipped: +inf and -inf if all are). Cell
// (i, j, k) blends voxels 8i to 8i + 8 along x (and so on), clamped to the
// field, so neighbouring cells share a layer of voxels.
#define STRUCTURED_REGULAR_MACROCELL_RANGES(T)                                 \
  static void StructuredRegular_macrocellRanges_##T(                           \
      const uniform SpatialField *uniform self,                                \
      uniform float *uniform ranges,                                           \
      uniform uint64 begin,                                                    \
      uniform uint64 end)                                                      \
  {                                                                            \
    const uniform StructuredRegularField &f = self->structuredRegular;         \
    const uniform int64 sx = f.dims[0];                                        \
    const uniform int64 sxy = sx * f.dims[1];                                  \
    const uniform uint64 cx = self->macrocellDims[0];                          \
    const uniform uint64 cxy = cx * self->macrocellDims[1];                    \
    for (uniform uint64 c = begin; c < end; c++) {                             \
      const uniform int x0 =                                                   \
          (uniform int)(c % cx) * STRUCTURED_REGULAR_MACROCELL_VOXELS;         \
      const uniform int y0 = (uniform int)((c / cx) % self->macrocellDims[1]) \
          * STRUCTURED_REGULAR_MACROCELL_VOXELS;                               \
      const uniform int z0 =                                                   \
          (uniform int)(c / cxy) * STRUCTURED_REGULAR_MACROCELL_VOXELS;        \
      const uniform int x1 =                                                   \
          min(x0 + STRUCTURED_REGULAR_MACROCELL_VOXELS, f.dims[0] - 1);        \
      const uniform int y1 =                                                   \
          min(y0 + STRUCTURED_REGULAR_MACROCELL_VOXELS, f.dims[1] - 1);        \
      const uniform int z1 =                                                   \
          min(z0 + STRUCTURED_REGULAR_MACROCELL_VOXELS, f.dims[2] - 1);        \
      float lo = floatbits(0x7f800000);                                        \
      float hi = floatbits(0xff800000);                                        \
      for (uniform int z = z0; z <= z1; z++) {                                 \
        for (uniform int y = y0; y <= y1; y++) {                               \
          const uniform int64 row = y * sx + z * sxy;                          \
          foreach (x = x0 ... x1 + 1) {                                         \
            const float v = voxel_##T(f, row + x);                             \
            /* comparisons with NaN are false, so NaN voxels are skipped */    \
            lo = v < lo ? v : lo;                                              \
            hi = v > hi ? v : hi;                                              \
          }                                                                    \
        }                                                                      \
      }                                                                        \
      ranges[2 * c] = reduce_min(lo);                                          \
      ranges[2 * c + 1] = reduce_max(hi);                                      \
    }                                                                          \
  }

STRUCTURED_REGULAR_MACROCELL_RANGES(float)
STRUCTURED_REGULAR_MACROCELL_RANGES(double)
STRUCTURED_REGULAR_MACROCELL_RANGES(uint8)
STRUCTURED_REGULAR_MACROCELL_RANGES(uint16)
STRUCTURED_REGULAR_MACROCELL_RANGES(int16)

#undef STRUCTURED_REGULAR_MACROCELL_RANGES

// Hook for voxels of ANARI type 'type', NULL if the type isn't supported
static uniform SpatialField_SampleFcn structuredRegularHook(
    uniform ANARIDataType type)
{
  switch (type) {
  case ANARI_FLOAT32:
    return StructuredRegular_sample_float;
  case ANARI_FLOAT64:
    return StructuredRegular_sample_double;
  case ANARI_UFIXED8:
    return StructuredRegular_sample_uint8;
  case ANARI_UFIXED16:
    return StructuredRegular_sample_uint16;
  case ANARI_FIXED16:
    return StructuredRegular_sample_int16;
  default:
    return NULL;
  }
}

// No hook (invalid), empty bounds, zero step size
static void setDefaults(uniform SpatialField *uniform self)
{
  self->sample = NULL;
  self->boundsLower = make_float3(floatbits(0x7f800000)); // +inf
  self->boundsUpper = make_float3(floatbits(0xff800000)); // -inf
  self->stepSize = 0.f;
  self->valueLower = floatbits(0x7f800000); // +inf
  self->valueUpper = floatbits(0xff800000); // -inf
  self->macrocellRanges = NULL;
  for (uniform int d = 0; d < 3; d++)
    self->macrocellDims[d] = 0;
  self->macrocellInvSize = make_float3(0.f);
  self->structuredRegular.voxels = NULL;
  for (uniform int d = 0; d < 3; d++)
    self->structuredRegular.dims[d] = 0;
  self->structuredRegular.origin = make_float3(0.f);
  self->structuredRegular.invSpacing = make_float3(1.f);
}

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_SpatialField()
{
  return sizeof(uniform SpatialField);
}

// See setDefaults()
export void SpatialField_setDefaults(void *uniform _self)
{
  setDefaults(object_cast<SpatialField>(_self));
}

// Installs the structuredRegular hook for 'voxels' of ANARI type 'voxelType'
// (x fastest) with size 'dims' and packed float3 'origin' and 'spacing', and
// sets the bounds, the step size (min(spacing) / 2) and the macrocell grid
// (cells of 8^3 voxel intervals); the value ranges come with
// SpatialField_setMacrocellRanges().
// Returns whether the field is valid: without voxels, with an empty size, an
// unsupported type or a non-positive spacing (which ray marching can't step
// through) it gets the defaults instead (see SpatialField_setDefaults()).
export uniform bool SpatialField_setStructuredRegular(void *uniform _self,
    const void *uniform voxels,
    uniform ANARIDataType voxelType,
    const uniform uint32 *uniform dims,
    const uniform float *uniform origin,
    const uniform float *uniform spacing)
{
  uniform SpatialField *uniform self = object_cast<SpatialField>(_self);
  setDefaults(self);

  const uniform SpatialField_SampleFcn hook =
      structuredRegularHook(voxelType);
  const uniform float3 s = load_float3(spacing);
  if (voxels == NULL || hook == NULL || dims[0] == 0 || dims[1] == 0
      || dims[2] == 0 || !(s.x > 0.f && s.y > 0.f && s.z > 0.f))
    return false;

  uniform StructuredRegularField &f = self->structuredRegular;
  f.voxels = (const uniform uint8 *uniform)voxels;
  for (uniform int d = 0; d < 3; d++)
    f.dims[d] = dims[d];
  f.origin = load_float3(origin);
  f.invSpacing = 1.f / s;

  const uniform float3 maxIndex =
      make_float3(dims[0] - 1.f, dims[1] - 1.f, dims[2] - 1.f);
  self->boundsLower = f.origin;
  self->boundsUpper = f.origin + maxIndex * s;
  self->stepSize = min(min(s.x, s.y), s.z) / 2.f;
  for (uniform int d = 0; d < 3; d++) {
    self->macrocellDims[d] = max(1,
        ((uniform int)dims[d] - 1 + STRUCTURED_REGULAR_MACROCELL_VOXELS - 1)
            / STRUCTURED_REGULAR_MACROCELL_VOXELS);
  }
  self->macrocellInvSize = 1.f / (STRUCTURED_REGULAR_MACROCELL_VOXELS * s);
  self->sample = hook;
  return true;
}

// The field's bounds as packed float3 'lower' and 'upper'
export void SpatialField_getBounds(const void *uniform _self,
    uniform float *uniform lower,
    uniform float *uniform upper)
{
  const uniform SpatialField *uniform self =
      (const uniform SpatialField *uniform)_self;
  store_float3(lower, self->boundsLower);
  store_float3(upper, self->boundsUpper);
}

export uniform float SpatialField_getStepSize(const void *uniform _self)
{
  const uniform SpatialField *uniform self =
      (const uniform SpatialField *uniform)_self;
  return self->stepSize;
}

// The number of the field's macrocells, 0 for an invalid field
export uniform uint64 SpatialField_numMacrocells(const void *uniform _self)
{
  const uniform SpatialField *uniform self =
      (const uniform SpatialField *uniform)_self;
  return (uniform uint64)self->macrocellDims[0] * self->macrocellDims[1]
      * self->macrocellDims[2];
}

// Computes the value ranges of macrocells [begin, end) into 'ranges'
// (packed float2, SpatialField_numMacrocells() of them; see
// SpatialField::macrocellRanges); cells may be computed in parallel
export void SpatialField_computeMacrocellRanges(const void *uniform _self,
    uniform float *uniform ranges,
    uniform uint64 begin,
    uniform uint64 end)
{
  const uniform SpatialField *uniform self =
      (const uniform SpatialField *uniform)_self;
  const uniform SpatialField_SampleFcn hook = self->sample;
  if (hook == StructuredRegular_sample_float)
    StructuredRegular_macrocellRanges_float(self, ranges, begin, end);
  else if (hook == StructuredRegular_sample_double)
    StructuredRegular_macrocellRanges_double(self, ranges, begin, end);
  else if (hook == StructuredRegular_sample_uint8)
    StructuredRegular_macrocellRanges_uint8(self, ranges, begin, end);
  else if (hook == StructuredRegular_sample_uint16)
    StructuredRegular_macrocellRanges_uint16(self, ranges, begin, end);
  else if (hook == StructuredRegular_sample_int16)
    StructuredRegular_macrocellRanges_int16(self, ranges, begin, end);
}

// Hands the field its macrocells' value ranges (computed by
// SpatialField_computeMacrocellRanges(), owned by the caller; NULL for
// none) and sets its value range from them
export void SpatialField_setMacrocellRanges(
    void *uniform _self, const uniform float *uniform ranges)
{
  uniform SpatialField *uniform self = object_cast<SpatialField>(_self);
  self->macrocellRanges = ranges;
  self->valueLower = floatbits(0x7f800000); // +inf
  self->valueUpper = floatbits(0xff800000); // -inf
  if (!ranges)
    return;
  const uniform uint64 n = SpatialField_numMacrocells(_self);
  float lo = self->valueLower;
  float hi = self->valueUpper;
  // foreach counts in int32
  for (uniform uint64 base = 0; base < n; base += (1 << 30)) {
    const uniform int count = (uniform int)min(n - base, (uint64)(1 << 30));
    foreach (i = 0 ... count) {
      lo = min(lo, ranges[2 * (base + i)]);
      hi = max(hi, ranges[2 * (base + i) + 1]);
    }
  }
  self->valueLower = reduce_min(lo);
  self->valueUpper = reduce_max(hi);
}
