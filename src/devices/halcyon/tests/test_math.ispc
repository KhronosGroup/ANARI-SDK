// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Exports halcyon_math.isph functions for comparison against linalg in
// test_math.cpp. Every test runs each case twice: once with uniform values in
// a uniform loop, once with varying values in a foreach. Because results are
// written with store_*() (whose uniform-pointer overloads only accept uniform
// values), uniform inputs producing uniform outputs is also checked at compile
// time.

#include "shading/halcyon_math.isph"

// Must match test_math.cpp
#define IN_STRIDE 80
#define OUT_STRIDE 32
// Input layout per case: vectors a/b/c at 0/4/8, scalar t at 12,
// 4x4 matrices M/N at 16/32, 3x3 matrices L/L2 at 48/64.

#define DEFINE_TEST(NAME, BODY)                                                \
  export void test_##NAME(const uniform float *uniform input,                     \
      uniform float *uniform outU,                                             \
      uniform float *uniform outV,                                             \
      uniform int n)                                                           \
  {                                                                            \
    for (uniform int i = 0; i < n; i++) {                                      \
      const uniform float *uniform I = input + i * IN_STRIDE;                     \
      uniform float *uniform O = outU + i * OUT_STRIDE;                        \
      BODY(uniform)                                                            \
    }                                                                          \
    foreach (i = 0 ... n) {                                                    \
      const uniform float *varying I = input + i * IN_STRIDE;                     \
      uniform float *varying O = outV + i * OUT_STRIDE;                        \
      BODY(varying)                                                            \
    }                                                                          \
  }

// Store helpers //////////////////////////////////////////////////////////////

#define DEFINE_STORES(UV)                                                      \
  inline void store_float(uniform float *UV O, UV float v)                     \
  {                                                                            \
    O[0] = v;                                                                  \
  }                                                                            \
  inline void store_linear3f(uniform float *UV O, const UV linear3f &m)        \
  {                                                                            \
    store_float3(O + 0, m.x);                                                  \
    store_float3(O + 3, m.y);                                                  \
    store_float3(O + 6, m.z);                                                  \
  }                                                                            \
  inline void store_affine3f(uniform float *UV O, const UV affine3f &a)        \
  {                                                                            \
    store_linear3f(O, a.l);                                                    \
    store_float3(O + 9, a.p);                                                  \
  }                                                                            \
  inline void store_mat4f(uniform float *UV O, const UV mat4f &m)              \
  {                                                                            \
    store_float4(O + 0, m.x);                                                  \
    store_float4(O + 4, m.y);                                                  \
    store_float4(O + 8, m.z);                                                  \
    store_float4(O + 12, m.w);                                                 \
  }                                                                            \
  inline void store_int(uniform float *UV O, UV int32 v)                       \
  {                                                                            \
    O[0] = (float)v;                                                           \
  }                                                                            \
  inline void store_int2(uniform float *UV O, UV int2 v)                       \
  {                                                                            \
    store_int(O + 0, v.x);                                                     \
    store_int(O + 1, v.y);                                                     \
  }                                                                            \
  inline void store_int3(uniform float *UV O, UV int3 v)                       \
  {                                                                            \
    store_int(O + 0, v.x);                                                     \
    store_int(O + 1, v.y);                                                     \
    store_int(O + 2, v.z);                                                     \
  }                                                                            \
  inline void store_int4(uniform float *UV O, UV int4 v)                       \
  {                                                                            \
    store_int(O + 0, v.x);                                                     \
    store_int(O + 1, v.y);                                                     \
    store_int(O + 2, v.z);                                                     \
    store_int(O + 3, v.w);                                                     \
  }                                                                            \
  inline void store_uint(uniform float *UV O, UV uint32 v)                     \
  {                                                                            \
    O[0] = (float)v;                                                           \
  }                                                                            \
  inline void store_uint2(uniform float *UV O, UV uint2 v)                     \
  {                                                                            \
    store_uint(O + 0, v.x);                                                    \
    store_uint(O + 1, v.y);                                                    \
  }                                                                            \
  inline void store_uint3(uniform float *UV O, UV uint3 v)                     \
  {                                                                            \
    store_uint(O + 0, v.x);                                                    \
    store_uint(O + 1, v.y);                                                    \
    store_uint(O + 2, v.z);                                                    \
  }                                                                            \
  inline void store_uint4(uniform float *UV O, UV uint4 v)                     \
  {                                                                            \
    store_uint(O + 0, v.x);                                                    \
    store_uint(O + 1, v.y);                                                    \
    store_uint(O + 2, v.z);                                                    \
    store_uint(O + 3, v.w);                                                    \
  }                                                                            \
  /* integer inputs are derived from float inputs: int(floor(10 * f)) and */  \
  /* uint(10 * |f|), mirrored in test_math.cpp                             */  \
  inline UV int32 load_int(const uniform float *UV p)                          \
  {                                                                            \
    return (int32)floor(10.f * p[0]);                                          \
  }                                                                            \
  inline UV uint32 load_uint(const uniform float *UV p)                        \
  {                                                                            \
    return (uint32)(10.f * abs(p[0]));                                         \
  }

DEFINE_STORES(uniform)
DEFINE_STORES(varying)

// Vector tests: EXPR is evaluated for widths 2, 3, 4 with a, b, c of that
// width and scalar t; results are packed as [2 | 3 | 4] (vector results) or
// [s2, s3, s4] (scalar results).

#define VEC_BODY(UV, EXPR)                                                     \
  {                                                                            \
    const UV float t = I[12];                                                  \
    {                                                                          \
      const UV float2 a = load_float2(I + 0), b = load_float2(I + 4),          \
                      c = load_float2(I + 8);                                  \
      store_float2(O + 0, EXPR);                                               \
    }                                                                          \
    {                                                                          \
      const UV float3 a = load_float3(I + 0), b = load_float3(I + 4),          \
                      c = load_float3(I + 8);                                  \
      store_float3(O + 2, EXPR);                                               \
    }                                                                          \
    {                                                                          \
      const UV float4 a = load_float4(I + 0), b = load_float4(I + 4),          \
                      c = load_float4(I + 8);                                  \
      store_float4(O + 5, EXPR);                                               \
    }                                                                          \
  }

#define SCALAR_BODY(UV, EXPR)                                                  \
  {                                                                            \
    {                                                                          \
      const UV float2 a = load_float2(I + 0), b = load_float2(I + 4);          \
      store_float(O + 0, EXPR);                                                \
    }                                                                          \
    {                                                                          \
      const UV float3 a = load_float3(I + 0), b = load_float3(I + 4);          \
      store_float(O + 1, EXPR);                                                \
    }                                                                          \
    {                                                                          \
      const UV float4 a = load_float4(I + 0), b = load_float4(I + 4);          \
      store_float(O + 2, EXPR);                                                \
    }                                                                          \
  }

#define min_BODY(UV) VEC_BODY(UV, min(a, b))
#define max_BODY(UV) VEC_BODY(UV, max(a, b))
#define abs_BODY(UV) VEC_BODY(UV, abs(a))
#define floor_BODY(UV) VEC_BODY(UV, floor(a))
#define fract_BODY(UV) VEC_BODY(UV, fract(a))
#define rcp_BODY(UV) VEC_BODY(UV, rcp(a))
#define clamp_BODY(UV) VEC_BODY(UV, clamp(a, min(b, c), max(b, c)))
#define clamp_scalar_BODY(UV) VEC_BODY(UV, clamp(a, -0.5f, 0.5f))
#define lerp_BODY(UV) VEC_BODY(UV, lerp(a, b, t))
#define lerp_vec_BODY(UV) VEC_BODY(UV, lerp(a, b, c))
#define normalize_BODY(UV) VEC_BODY(UV, normalize(a))
#define safe_normalize_BODY(UV) VEC_BODY(UV, safe_normalize(a))
#define safe_normalize_zero_BODY(UV) VEC_BODY(UV, safe_normalize(a * 0.f))
#define dot_BODY(UV) SCALAR_BODY(UV, dot(a, b))
#define length_BODY(UV) SCALAR_BODY(UV, length(a))
#define length2_BODY(UV) SCALAR_BODY(UV, length2(a))
#define distance_BODY(UV) SCALAR_BODY(UV, distance(a, b))
#define distance2_BODY(UV) SCALAR_BODY(UV, distance2(a, b))
#define sum_BODY(UV) SCALAR_BODY(UV, sum(a))
#define minelem_BODY(UV) SCALAR_BODY(UV, minelem(a))
#define maxelem_BODY(UV) SCALAR_BODY(UV, maxelem(a))

DEFINE_TEST(min, min_BODY)
DEFINE_TEST(max, max_BODY)
DEFINE_TEST(abs, abs_BODY)
DEFINE_TEST(floor, floor_BODY)
DEFINE_TEST(fract, fract_BODY)
DEFINE_TEST(rcp, rcp_BODY)
DEFINE_TEST(clamp, clamp_BODY)
DEFINE_TEST(clamp_scalar, clamp_scalar_BODY)
DEFINE_TEST(lerp, lerp_BODY)
DEFINE_TEST(lerp_vec, lerp_vec_BODY)
DEFINE_TEST(normalize, normalize_BODY)
DEFINE_TEST(safe_normalize, safe_normalize_BODY)
DEFINE_TEST(safe_normalize_zero, safe_normalize_zero_BODY)
DEFINE_TEST(dot, dot_BODY)
DEFINE_TEST(length, length_BODY)
DEFINE_TEST(length2, length2_BODY)
DEFINE_TEST(distance, distance_BODY)
DEFINE_TEST(distance2, distance2_BODY)
DEFINE_TEST(sum, sum_BODY)
DEFINE_TEST(minelem, minelem_BODY)
DEFINE_TEST(maxelem, maxelem_BODY)

#define cross_BODY(UV)                                                         \
  store_float3(O, cross(load_float3(I + 0), load_float3(I + 4)));
DEFINE_TEST(cross, cross_BODY)

#define make_BODY(UV)                                                          \
  {                                                                            \
    const UV float s = I[0];                                                   \
    store_float2(O + 0, make_float2(I[1], I[2]));                              \
    store_float2(O + 2, make_float2(s));                                       \
    store_float3(O + 4, make_float3(I[1], I[2], I[3]));                        \
    store_float3(O + 7, make_float3(s));                                       \
    store_float3(O + 10, make_float3(load_float4(I + 4)));                     \
    store_float4(O + 13, make_float4(I[1], I[2], I[3], I[4]));                 \
    store_float4(O + 17, make_float4(s));                                      \
    store_float4(O + 21, make_float4(load_float3(I + 5), s));                  \
  }
DEFINE_TEST(make, make_BODY)

// Integer vector tests: layout [int2 | int3 | int4 | uint2 | uint3 | uint4]
// for vector results, [i2, i3, i4, u2, u3, u4] for scalar results.

#define INT_BODY(UV, EXPR, STORE_I2, STORE_I3, STORE_I4, OFS)                  \
  {                                                                            \
    {                                                                          \
      const UV int2 a = make_int2(load_int(I + 0), load_int(I + 1));           \
      const UV int2 b = make_int2(load_int(I + 4), load_int(I + 5));           \
      const UV int2 c = make_int2(load_int(I + 8), load_int(I + 9));           \
      STORE_I2(O + OFS(0), EXPR);                                              \
    }                                                                          \
    {                                                                          \
      const UV int3 a =                                                        \
          make_int3(load_int(I + 0), load_int(I + 1), load_int(I + 2));        \
      const UV int3 b =                                                        \
          make_int3(load_int(I + 4), load_int(I + 5), load_int(I + 6));        \
      const UV int3 c =                                                        \
          make_int3(load_int(I + 8), load_int(I + 9), load_int(I + 10));       \
      STORE_I3(O + OFS(1), EXPR);                                              \
    }                                                                          \
    {                                                                          \
      const UV int4 a = make_int4(                                             \
          load_int(I + 0), load_int(I + 1), load_int(I + 2), load_int(I + 3)); \
      const UV int4 b = make_int4(                                             \
          load_int(I + 4), load_int(I + 5), load_int(I + 6), load_int(I + 7)); \
      const UV int4 c = make_int4(load_int(I + 8),                             \
          load_int(I + 9),                                                     \
          load_int(I + 10),                                                    \
          load_int(I + 11));                                                   \
      STORE_I4(O + OFS(2), EXPR);                                              \
    }                                                                          \
  }

#define UINT_BODY(UV, EXPR, STORE_U2, STORE_U3, STORE_U4, OFS)                 \
  {                                                                            \
    {                                                                          \
      const UV uint2 a = make_uint2(load_uint(I + 0), load_uint(I + 1));       \
      const UV uint2 b = make_uint2(load_uint(I + 4), load_uint(I + 5));       \
      const UV uint2 c = make_uint2(load_uint(I + 8), load_uint(I + 9));       \
      STORE_U2(O + OFS(3), EXPR);                                              \
    }                                                                          \
    {                                                                          \
      const UV uint3 a =                                                       \
          make_uint3(load_uint(I + 0), load_uint(I + 1), load_uint(I + 2));    \
      const UV uint3 b =                                                       \
          make_uint3(load_uint(I + 4), load_uint(I + 5), load_uint(I + 6));    \
      const UV uint3 c =                                                       \
          make_uint3(load_uint(I + 8), load_uint(I + 9), load_uint(I + 10));   \
      STORE_U3(O + OFS(4), EXPR);                                              \
    }                                                                          \
    {                                                                          \
      const UV uint4 a = make_uint4(load_uint(I + 0),                          \
          load_uint(I + 1),                                                    \
          load_uint(I + 2),                                                    \
          load_uint(I + 3));                                                   \
      const UV uint4 b = make_uint4(load_uint(I + 4),                          \
          load_uint(I + 5),                                                    \
          load_uint(I + 6),                                                    \
          load_uint(I + 7));                                                   \
      const UV uint4 c = make_uint4(load_uint(I + 8),                          \
          load_uint(I + 9),                                                    \
          load_uint(I + 10),                                                   \
          load_uint(I + 11));                                                  \
      STORE_U4(O + OFS(5), EXPR);                                              \
    }                                                                          \
  }

#define VEC_OFS(k) ((k) == 0 ? 0 : (k) == 1 ? 2 : (k) == 2 ? 5 : (k) == 3 ? 9 : (k) == 4 ? 11 : 14)
#define SCALAR_OFS(k) (k)

#define IVEC_BODY(UV, EXPR)                                                    \
  INT_BODY(UV, EXPR, store_int2, store_int3, store_int4, VEC_OFS)              \
  UINT_BODY(UV, EXPR, store_uint2, store_uint3, store_uint4, VEC_OFS)
#define ISCALAR_BODY(UV, EXPR)                                                 \
  INT_BODY(UV, EXPR, store_int, store_int, store_int, SCALAR_OFS)              \
  UINT_BODY(UV, EXPR, store_uint, store_uint, store_uint, SCALAR_OFS)

#define imin_BODY(UV) IVEC_BODY(UV, min(a, b))
#define imax_BODY(UV) IVEC_BODY(UV, max(a, b))
#define iclamp_BODY(UV) IVEC_BODY(UV, clamp(a, min(b, c), max(b, c)))
#define isum_BODY(UV) ISCALAR_BODY(UV, sum(a))
#define iminelem_BODY(UV) ISCALAR_BODY(UV, minelem(a))
#define imaxelem_BODY(UV) ISCALAR_BODY(UV, maxelem(a))
#define iabs_BODY(UV) INT_BODY(UV, abs(a), store_int2, store_int3, store_int4, VEC_OFS)

DEFINE_TEST(imin, imin_BODY)
DEFINE_TEST(imax, imax_BODY)
DEFINE_TEST(iclamp, iclamp_BODY)
DEFINE_TEST(isum, isum_BODY)
DEFINE_TEST(iminelem, iminelem_BODY)
DEFINE_TEST(imaxelem, imaxelem_BODY)
DEFINE_TEST(iabs, iabs_BODY)

// Matrix tests ///////////////////////////////////////////////////////////////

#define l3_transpose_BODY(UV) store_linear3f(O, transpose(load_linear3f(I + 48)));
#define l3_determinant_BODY(UV) store_float(O, determinant(load_linear3f(I + 48)));
#define l3_inverse_BODY(UV) store_linear3f(O, inverse(load_linear3f(I + 48)));
#define l3_mul_BODY(UV)                                                        \
  store_linear3f(O, mul(load_linear3f(I + 48), load_linear3f(I + 64)));
#define l3_mul_vec_BODY(UV)                                                    \
  store_float3(O, mul(load_linear3f(I + 48), load_float3(I + 0)));
#define l3_xfmVector_BODY(UV)                                                  \
  store_float3(O, xfmVector(load_linear3f(I + 48), load_float3(I + 0)));
#define l3_xfmNormal_BODY(UV)                                                  \
  store_float3(O, xfmNormal(load_linear3f(I + 48), load_float3(I + 0)));
/* uniform structs don't promote to varying in overload resolution: assign */
#define l3_identity_BODY(UV)                                                   \
  {                                                                            \
    const UV linear3f m = make_linear3f_identity();                            \
    store_linear3f(O, m);                                                      \
  }

DEFINE_TEST(l3_transpose, l3_transpose_BODY)
DEFINE_TEST(l3_determinant, l3_determinant_BODY)
DEFINE_TEST(l3_inverse, l3_inverse_BODY)
DEFINE_TEST(l3_mul, l3_mul_BODY)
DEFINE_TEST(l3_mul_vec, l3_mul_vec_BODY)
DEFINE_TEST(l3_xfmVector, l3_xfmVector_BODY)
DEFINE_TEST(l3_xfmNormal, l3_xfmNormal_BODY)
DEFINE_TEST(l3_identity, l3_identity_BODY)

#define a3_load_BODY(UV) store_affine3f(O, load_affine3f(I + 16));
#define a3_from_mat4f_BODY(UV)                                                 \
  store_affine3f(O, make_affine3f(load_mat4f(I + 16)));
#define a3_to_mat4f_BODY(UV) store_mat4f(O, make_mat4f(load_affine3f(I + 16)));
#define a3_inverse_BODY(UV) store_affine3f(O, inverse(load_affine3f(I + 16)));
#define a3_mul_BODY(UV)                                                        \
  store_affine3f(O, mul(load_affine3f(I + 16), load_affine3f(I + 32)));
#define a3_xfmPoint_BODY(UV)                                                   \
  store_float3(O, xfmPoint(load_affine3f(I + 16), load_float3(I + 0)));
#define a3_xfmVector_BODY(UV)                                                  \
  store_float3(O, xfmVector(load_affine3f(I + 16), load_float3(I + 0)));
#define a3_xfmNormal_BODY(UV)                                                  \
  store_float3(O, xfmNormal(load_affine3f(I + 16), load_float3(I + 0)));
#define a3_identity_BODY(UV)                                                   \
  {                                                                            \
    const UV affine3f a = make_affine3f_identity();                            \
    store_affine3f(O, a);                                                      \
  }

DEFINE_TEST(a3_load, a3_load_BODY)
DEFINE_TEST(a3_from_mat4f, a3_from_mat4f_BODY)
DEFINE_TEST(a3_to_mat4f, a3_to_mat4f_BODY)
DEFINE_TEST(a3_inverse, a3_inverse_BODY)
DEFINE_TEST(a3_mul, a3_mul_BODY)
DEFINE_TEST(a3_xfmPoint, a3_xfmPoint_BODY)
DEFINE_TEST(a3_xfmVector, a3_xfmVector_BODY)
DEFINE_TEST(a3_xfmNormal, a3_xfmNormal_BODY)
DEFINE_TEST(a3_identity, a3_identity_BODY)

#define m4_transpose_BODY(UV) store_mat4f(O, transpose(load_mat4f(I + 16)));
#define m4_determinant_BODY(UV) store_float(O, determinant(load_mat4f(I + 16)));
#define m4_inverse_BODY(UV) store_mat4f(O, inverse(load_mat4f(I + 16)));
#define m4_mul_BODY(UV)                                                        \
  store_mat4f(O, mul(load_mat4f(I + 16), load_mat4f(I + 32)));
#define m4_mul_vec_BODY(UV)                                                    \
  store_float4(O, mul(load_mat4f(I + 16), load_float4(I + 0)));
#define m4_identity_BODY(UV)                                                   \
  {                                                                            \
    const UV mat4f m = make_mat4f_identity();                                  \
    store_mat4f(O, m);                                                         \
  }

DEFINE_TEST(m4_transpose, m4_transpose_BODY)
DEFINE_TEST(m4_determinant, m4_determinant_BODY)
DEFINE_TEST(m4_inverse, m4_inverse_BODY)
DEFINE_TEST(m4_mul, m4_mul_BODY)
DEFINE_TEST(m4_mul_vec, m4_mul_vec_BODY)
DEFINE_TEST(m4_identity, m4_identity_BODY)

// Mixed variability: the matrix always comes from case 0 through a uniform
// pointer, so in the foreach pass it is uniform while the vector is varying.
// Layout: [xfmPoint(3) | xfmVector(3) | xfmNormal(3) | mul(mat4f)(4) |
//          mul(linear3f)(3)]

#define mixed_BODY(UV)                                                         \
  {                                                                            \
    const uniform affine3f A = load_affine3f(input + 16);                         \
    const uniform mat4f M = load_mat4f(input + 16);                               \
    const uniform linear3f L = load_linear3f(input + 48);                         \
    const UV float3 v = load_float3(I + 0);                                    \
    store_float3(O + 0, xfmPoint(A, v));                                       \
    store_float3(O + 3, xfmVector(A, v));                                      \
    store_float3(O + 6, xfmNormal(A, v));                                      \
    store_float4(O + 9, mul(M, load_float4(I + 0)));                           \
    store_float3(O + 13, mul(L, v));                                           \
  }
DEFINE_TEST(mixed, mixed_BODY)
