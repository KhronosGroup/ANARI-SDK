// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// How the native materials' tests (path_pbm, path_material,
// native_materials) compare native ISPC results with the vendored C++.
// PathPbm.ispc is built without fused multiply-adds and with the system's
// math library, so it rounds as the C++ does where the C++ isn't contracted
// to FMAs or built with fast math: tests/CMakeLists.txt probes the build's
// flags and sets HALCYON_NATIVE_BITWISE to 1 there (x86-64 without FMA, or
// with -ffp-contract=off) and to 0 elsewhere (aarch64, Apple clang on
// arm64, -march=native, -ffast-math). The default x86-64 build compares
// bitwise; the others within rounding.

#pragma once

#include <algorithm>

#ifndef HALCYON_NATIVE_BITWISE
#error "HALCYON_NATIVE_BITWISE is set by tests/CMakeLists.txt"
#endif

namespace native_rounding {

constexpr bool BITWISE = HALCYON_NATIVE_BITWISE;

// Within rounding: native matte's tolerance for its points, relative
// to max(1, |value|)
constexpr float ROUNDING = 1e-4f;

// How much a GGX lobe of roughness alpha (MaterialXCpp's, the square of
// glTF's roughness) amplifies rounding, as a factor on ROUNDING. Its
// distribution is a peak of width alpha, so its value at a direction moves
// by about (the direction's rounding) / alpha^2 relative to itself. With the
// C++ contracted (-march=native), path_pbm's evaluations, pdfs and samples
// differ by err with sqrt(err / ROUNDING) * alpha below 0.1 at all but 44 of
// about 560000 values, and below 0.2 at all but 4: the factor is
// (0.2 / alpha)^2, four times the error the bound of 0.1 allows. It is loose
// near alpha 1e-3 (a factor of 4e4), where the values themselves are that
// ill-conditioned. Delta lobes (alpha below 1e-3) have no peak to evaluate,
// and agree within ROUNDING.
// Within rounding, the few values (path_pbm) or points (path_material) that
// may fall beyond their tolerance must still be within OUTLIER times it, so
// that "a few" can't hide a gross error: with -march=native the worst of
// them is 6.2 times its tolerance (path_pbm's grazing refractions), the
// others about 1e-3 relative. The conditioning itself isn't capped: GGX
// values near alpha 1e-3 really differ by up to 0.84 relative, within
// 0.89 times their tolerance.
constexpr float OUTLIER = 100.f;

inline float ggxConditioning(float alpha)
{
  return alpha < 1e-3f ? 1.f
                       : std::max(1.f, (0.2f / alpha) * (0.2f / alpha));
}

} // namespace native_rounding
