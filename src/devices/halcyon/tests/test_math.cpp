// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Compares shading/halcyon_math.isph (via test_math.ispc) against linalg.

#include "HalcyonMath.h"
#include "test_math_ispc.h"
// std
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <vector>

namespace {

// Must match test_math.ispc
constexpr int IN_STRIDE = 80;
constexpr int OUT_STRIDE = 32;
constexpr int NUM_CASES = 67; // deliberately not a multiple of any SIMD width

template <typename T, int N>
using vec = linalg::vec<T, N>;
using mat3 = linalg::mat<float, 3, 3>;
using mat4 = linalg::mat<float, 4, 4>;

// Load/store helpers //

template <int N>
vec<float, N> ld(const float *p)
{
  vec<float, N> v;
  for (int i = 0; i < N; i++)
    v[i] = p[i];
  return v;
}

int ldInt(const float *p)
{
  return int(std::floor(10.f * p[0]));
}

uint32_t ldUint(const float *p)
{
  return uint32_t(10.f * std::fabs(p[0]));
}

template <typename T, int N>
vec<T, N> ldI(const float *p)
{
  vec<T, N> v;
  for (int i = 0; i < N; i++)
    v[i] = std::is_signed_v<T> ? T(ldInt(p + i)) : T(ldUint(p + i));
  return v;
}

template <typename T, int N>
void st(float *o, const vec<T, N> &v)
{
  for (int i = 0; i < N; i++)
    o[i] = float(v[i]);
}

template <typename T>
void st(float *o, T v)
{
  o[0] = float(v);
}

mat3 ldMat3(const float *p)
{
  mat3 m;
  std::memcpy(&m, p, sizeof(m));
  return m;
}

mat4 ldMat4(const float *p)
{
  mat4 m;
  std::memcpy(&m, p, sizeof(m));
  return m;
}

// affine3f loads ignore the bottom row of the 4x4
mat4 ldAffine(const float *p)
{
  mat4 m = ldMat4(p);
  m[0][3] = m[1][3] = m[2][3] = 0.f;
  m[3][3] = 1.f;
  return m;
}

mat3 linearPart(const mat4 &m)
{
  return mat3(m[0].xyz(), m[1].xyz(), m[2].xyz());
}

void stMat3(float *o, const mat3 &m)
{
  std::memcpy(o, &m, sizeof(m));
}

void stMat4(float *o, const mat4 &m)
{
  std::memcpy(o, &m, sizeof(m));
}

void stAffine(float *o, const mat4 &m)
{
  for (int c = 0; c < 4; c++)
    st(o + 3 * c, m[c].xyz());
}

// Reference function adapters //

// 'in0' is case 0's input (for the mixed-variability test), 'I' this case's
using RefFcn = std::function<void(const float *in0, const float *I, float *O)>;

template <typename F>
RefFcn vecRef(F f)
{
  return [f](const float *, const float *I, float *O) {
    const float t = I[12];
    st(O + 0, f(ld<2>(I), ld<2>(I + 4), ld<2>(I + 8), t));
    st(O + 2, f(ld<3>(I), ld<3>(I + 4), ld<3>(I + 8), t));
    st(O + 5, f(ld<4>(I), ld<4>(I + 4), ld<4>(I + 8), t));
  };
}

template <typename F>
RefFcn scalarRef(F f)
{
  return [f](const float *, const float *I, float *O) {
    O[0] = f(ld<2>(I), ld<2>(I + 4));
    O[1] = f(ld<3>(I), ld<3>(I + 4));
    O[2] = f(ld<4>(I), ld<4>(I + 4));
  };
}

// integer layouts: [int2 | int3 | int4 | uint2 | uint3 | uint4] or scalars
template <bool WITH_UNSIGNED = true, typename F>
RefFcn ivecRef(F f)
{
  return [f](const float *, const float *I, float *O) {
    st(O + 0, f(ldI<int, 2>(I), ldI<int, 2>(I + 4), ldI<int, 2>(I + 8)));
    st(O + 2, f(ldI<int, 3>(I), ldI<int, 3>(I + 4), ldI<int, 3>(I + 8)));
    st(O + 5, f(ldI<int, 4>(I), ldI<int, 4>(I + 4), ldI<int, 4>(I + 8)));
    if constexpr (WITH_UNSIGNED) {
      using u = uint32_t;
      st(O + 9, f(ldI<u, 2>(I), ldI<u, 2>(I + 4), ldI<u, 2>(I + 8)));
      st(O + 11, f(ldI<u, 3>(I), ldI<u, 3>(I + 4), ldI<u, 3>(I + 8)));
      st(O + 14, f(ldI<u, 4>(I), ldI<u, 4>(I + 4), ldI<u, 4>(I + 8)));
    }
  };
}

template <typename F>
RefFcn iscalarRef(F f)
{
  return [f](const float *, const float *I, float *O) {
    using u = uint32_t;
    O[0] = float(f(ldI<int, 2>(I)));
    O[1] = float(f(ldI<int, 3>(I)));
    O[2] = float(f(ldI<int, 4>(I)));
    O[3] = float(f(ldI<u, 2>(I)));
    O[4] = float(f(ldI<u, 3>(I)));
    O[5] = float(f(ldI<u, 4>(I)));
  };
}

template <typename V>
V splat(float s)
{
  return V(s);
}

// Test table //

using IspcFcn = void (*)(const float *, float *, float *, int32_t);

struct Test
{
  const char *name;
  IspcFcn ispcFcn;
  int numOutputs;
  float tolerance; // |result - ref| <= tolerance * (1 + |ref|)
  RefFcn ref;
};

constexpr float TOL = 1e-5f;
constexpr float TOL_INV = 1e-4f; // inverses/determinants accumulate more error

#define TEST(name, n, tol, ...)                                                \
  Test{#name, ispc::test_##name, n, tol, __VA_ARGS__}

std::vector<Test> makeTests()
{
  return {
      // Float vectors //
      TEST(min, 9, TOL, vecRef([](auto a, auto b, auto, float) {
        return linalg::min(a, b);
      })),
      TEST(max, 9, TOL, vecRef([](auto a, auto b, auto, float) {
        return linalg::max(a, b);
      })),
      TEST(abs, 9, TOL, vecRef([](auto a, auto, auto, float) {
        return linalg::abs(a);
      })),
      TEST(floor, 9, TOL, vecRef([](auto a, auto, auto, float) {
        return linalg::floor(a);
      })),
      TEST(fract, 9, TOL, vecRef([](auto a, auto, auto, float) {
        return a - linalg::floor(a);
      })),
      TEST(rcp, 9, TOL, vecRef([](auto a, auto, auto, float) {
        return 1.f / a;
      })),
      TEST(clamp, 9, TOL, vecRef([](auto a, auto b, auto c, float) {
        return linalg::clamp(a, linalg::min(b, c), linalg::max(b, c));
      })),
      TEST(clamp_scalar, 9, TOL, vecRef([](auto a, auto, auto, float) {
        using V = decltype(a);
        return linalg::clamp(a, splat<V>(-0.5f), splat<V>(0.5f));
      })),
      TEST(lerp, 9, TOL, vecRef([](auto a, auto b, auto, float t) {
        return linalg::lerp(a, b, t);
      })),
      TEST(lerp_vec, 9, TOL, vecRef([](auto a, auto b, auto c, float) {
        return linalg::lerp(a, b, c);
      })),
      TEST(normalize, 9, TOL, vecRef([](auto a, auto, auto, float) {
        return linalg::normalize(a);
      })),
      TEST(safe_normalize, 9, TOL, vecRef([](auto a, auto, auto, float) {
        return linalg::normalize(a);
      })),
      TEST(safe_normalize_zero, 9, TOL, vecRef([](auto a, auto, auto, float) {
        return a * 0.f;
      })),
      TEST(dot, 3, TOL, scalarRef([](auto a, auto b) {
        return linalg::dot(a, b);
      })),
      TEST(length, 3, TOL, scalarRef([](auto a, auto) {
        return linalg::length(a);
      })),
      TEST(length2, 3, TOL, scalarRef([](auto a, auto) {
        return linalg::length2(a);
      })),
      TEST(distance, 3, TOL, scalarRef([](auto a, auto b) {
        return linalg::distance(a, b);
      })),
      TEST(distance2, 3, TOL, scalarRef([](auto a, auto b) {
        return linalg::distance2(a, b);
      })),
      TEST(sum, 3, TOL, scalarRef([](auto a, auto) {
        return linalg::sum(a);
      })),
      TEST(minelem, 3, TOL, scalarRef([](auto a, auto) {
        return linalg::minelem(a);
      })),
      TEST(maxelem, 3, TOL, scalarRef([](auto a, auto) {
        return linalg::maxelem(a);
      })),
      TEST(cross,
          3,
          TOL,
          [](const float *, const float *I, float *O) {
            st(O, linalg::cross(ld<3>(I), ld<3>(I + 4)));
          }),
      TEST(make,
          25,
          TOL,
          [](const float *, const float *I, float *O) {
            const float s = I[0];
            st(O + 0, vec<float, 2>(I[1], I[2]));
            st(O + 2, vec<float, 2>(s));
            st(O + 4, vec<float, 3>(I[1], I[2], I[3]));
            st(O + 7, vec<float, 3>(s));
            st(O + 10, ld<4>(I + 4).xyz());
            st(O + 13, vec<float, 4>(I[1], I[2], I[3], I[4]));
            st(O + 17, vec<float, 4>(s));
            st(O + 21, vec<float, 4>(ld<3>(I + 5), s));
          }),

      // Integer vectors //
      TEST(imin, 18, 0.f, ivecRef([](auto a, auto b, auto) {
        return linalg::min(a, b);
      })),
      TEST(imax, 18, 0.f, ivecRef([](auto a, auto b, auto) {
        return linalg::max(a, b);
      })),
      TEST(iclamp, 18, 0.f, ivecRef([](auto a, auto b, auto c) {
        return linalg::clamp(a, linalg::min(b, c), linalg::max(b, c));
      })),
      TEST(iabs,
          9,
          0.f,
          ivecRef<false>([](auto a, auto, auto) { return linalg::abs(a); })),
      TEST(isum, 6, 0.f, iscalarRef([](auto a) { return linalg::sum(a); })),
      TEST(iminelem,
          6,
          0.f,
          iscalarRef([](auto a) { return linalg::minelem(a); })),
      TEST(imaxelem,
          6,
          0.f,
          iscalarRef([](auto a) { return linalg::maxelem(a); })),

      // linear3f //
      TEST(l3_transpose,
          9,
          TOL,
          [](const float *, const float *I, float *O) {
            stMat3(O, linalg::transpose(ldMat3(I + 48)));
          }),
      TEST(l3_determinant,
          1,
          TOL_INV,
          [](const float *, const float *I, float *O) {
            O[0] = linalg::determinant(ldMat3(I + 48));
          }),
      TEST(l3_inverse,
          9,
          TOL_INV,
          [](const float *, const float *I, float *O) {
            stMat3(O, linalg::inverse(ldMat3(I + 48)));
          }),
      TEST(l3_mul,
          9,
          TOL,
          [](const float *, const float *I, float *O) {
            stMat3(O, linalg::mul(ldMat3(I + 48), ldMat3(I + 64)));
          }),
      TEST(l3_mul_vec,
          3,
          TOL,
          [](const float *, const float *I, float *O) {
            st(O, linalg::mul(ldMat3(I + 48), ld<3>(I)));
          }),
      TEST(l3_xfmVector,
          3,
          TOL,
          [](const float *, const float *I, float *O) {
            st(O, linalg::mul(ldMat3(I + 48), ld<3>(I)));
          }),
      TEST(l3_xfmNormal,
          3,
          TOL_INV,
          [](const float *, const float *I, float *O) {
            const auto nm = linalg::transpose(linalg::inverse(ldMat3(I + 48)));
            st(O, linalg::mul(nm, ld<3>(I)));
          }),
      TEST(l3_identity,
          9,
          0.f,
          [](const float *, const float *, float *O) {
            stMat3(O, mat3(linalg::identity));
          }),

      // affine3f //
      TEST(a3_load,
          12,
          0.f,
          [](const float *, const float *I, float *O) {
            stAffine(O, ldAffine(I + 16));
          }),
      TEST(a3_from_mat4f,
          12,
          0.f,
          [](const float *, const float *I, float *O) {
            stAffine(O, ldAffine(I + 16));
          }),
      TEST(a3_to_mat4f,
          16,
          0.f,
          [](const float *, const float *I, float *O) {
            stMat4(O, ldAffine(I + 16));
          }),
      TEST(a3_inverse,
          12,
          TOL_INV,
          [](const float *, const float *I, float *O) {
            stAffine(O, linalg::inverse(ldAffine(I + 16)));
          }),
      TEST(a3_mul,
          12,
          TOL,
          [](const float *, const float *I, float *O) {
            stAffine(O, linalg::mul(ldAffine(I + 16), ldAffine(I + 32)));
          }),
      TEST(a3_xfmPoint,
          3,
          TOL,
          [](const float *, const float *I, float *O) {
            const auto p = vec<float, 4>(ld<3>(I), 1.f);
            st(O, linalg::mul(ldAffine(I + 16), p).xyz());
          }),
      TEST(a3_xfmVector,
          3,
          TOL,
          [](const float *, const float *I, float *O) {
            const auto v = vec<float, 4>(ld<3>(I), 0.f);
            st(O, linalg::mul(ldAffine(I + 16), v).xyz());
          }),
      TEST(a3_xfmNormal,
          3,
          TOL_INV,
          [](const float *, const float *I, float *O) {
            const auto l = linearPart(ldAffine(I + 16));
            st(O, linalg::mul(linalg::transpose(linalg::inverse(l)), ld<3>(I)));
          }),
      TEST(a3_identity,
          12,
          0.f,
          [](const float *, const float *, float *O) {
            stAffine(O, mat4(linalg::identity));
          }),

      // mat4f //
      TEST(m4_transpose,
          16,
          TOL,
          [](const float *, const float *I, float *O) {
            stMat4(O, linalg::transpose(ldMat4(I + 16)));
          }),
      TEST(m4_determinant,
          1,
          TOL_INV,
          [](const float *, const float *I, float *O) {
            O[0] = linalg::determinant(ldMat4(I + 16));
          }),
      TEST(m4_inverse,
          16,
          TOL_INV,
          [](const float *, const float *I, float *O) {
            stMat4(O, linalg::inverse(ldMat4(I + 16)));
          }),
      TEST(m4_mul,
          16,
          TOL,
          [](const float *, const float *I, float *O) {
            stMat4(O, linalg::mul(ldMat4(I + 16), ldMat4(I + 32)));
          }),
      TEST(m4_mul_vec,
          4,
          TOL,
          [](const float *, const float *I, float *O) {
            st(O, linalg::mul(ldMat4(I + 16), ld<4>(I)));
          }),
      TEST(m4_identity,
          16,
          0.f,
          [](const float *, const float *, float *O) {
            stMat4(O, mat4(linalg::identity));
          }),

      // Mixed variability (uniform matrix from case 0, varying vector) //
      TEST(mixed,
          16,
          TOL_INV,
          [](const float *in0, const float *I, float *O) {
            const auto A = ldAffine(in0 + 16);
            const auto M = ldMat4(in0 + 16);
            const auto L = ldMat3(in0 + 48);
            const auto v = ld<3>(I);
            st(O + 0, linalg::mul(A, vec<float, 4>(v, 1.f)).xyz());
            st(O + 3, linalg::mul(A, vec<float, 4>(v, 0.f)).xyz());
            const auto nm = linalg::transpose(linalg::inverse(linearPart(A)));
            st(O + 6, linalg::mul(nm, v));
            st(O + 9, linalg::mul(M, ld<4>(I)));
            st(O + 13, linalg::mul(L, v));
          }),
  };
}

#undef TEST

std::vector<float> makeInputs()
{
  std::mt19937 rng(1234);
  std::uniform_real_distribution<float> dist(-2.f, 2.f);

  std::vector<float> in(NUM_CASES * IN_STRIDE);
  for (auto &v : in)
    v = dist(rng);

  // keep matrices well conditioned so inverses are comparable
  for (int i = 0; i < NUM_CASES; i++) {
    float *I = in.data() + i * IN_STRIDE;
    for (int d : {0, 5, 10, 15}) {
      I[16 + d] += 3.f;
      I[32 + d] += 3.f;
    }
    for (int d : {0, 4, 8}) {
      I[48 + d] += 3.f;
      I[64 + d] += 3.f;
    }
  }

  return in;
}

bool close(float result, float ref, float tolerance)
{
  if (std::isnan(result) || std::isnan(ref))
    return false;
  return std::fabs(result - ref) <= tolerance * (1.f + std::fabs(ref));
}

// Returns number of mismatching values, reporting the first one
int compare(const Test &t,
    const char *variability,
    const std::vector<float> &out,
    const std::vector<float> &ref)
{
  int failures = 0;
  for (int i = 0; i < NUM_CASES; i++) {
    for (int j = 0; j < t.numOutputs; j++) {
      const int idx = i * OUT_STRIDE + j;
      if (close(out[idx], ref[idx], t.tolerance))
        continue;
      if (failures++ == 0) {
        std::printf("  FAIL %s (%s): case %d, output %d: got %g, expected %g\n",
            t.name,
            variability,
            i,
            j,
            out[idx],
            ref[idx]);
      }
    }
  }
  return failures;
}

} // namespace

int main()
{
  const auto in = makeInputs();
  const auto tests = makeTests();

  int failedTests = 0;

  for (const auto &t : tests) {
    std::vector<float> outU(NUM_CASES * OUT_STRIDE, NAN);
    std::vector<float> outV(NUM_CASES * OUT_STRIDE, NAN);
    std::vector<float> ref(NUM_CASES * OUT_STRIDE, NAN);

    t.ispcFcn(in.data(), outU.data(), outV.data(), NUM_CASES);
    for (int i = 0; i < NUM_CASES; i++)
      t.ref(in.data(), in.data() + i * IN_STRIDE, ref.data() + i * OUT_STRIDE);

    const int failures = compare(t, "uniform", outU, ref)
        + compare(t, "varying", outV, ref);
    if (failures)
      failedTests++;
  }

  std::printf("%zu tests, %d failed\n", tests.size(), failedTests);
  return failedTests == 0 ? 0 : 1;
}
