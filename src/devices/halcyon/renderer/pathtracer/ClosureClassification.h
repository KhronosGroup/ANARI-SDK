// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0

#pragma once

// Transport classification of evaluated closures, ported from Typhoon's
// renderer/integrator/closureClassification.cpp (not vendored: the rest of
// that directory needs OpenUSD). Keep in step with it when the pin moves.

namespace mxcpp {
struct SurfaceClosure;
}

namespace halcyon::pathtracer {

// Typhoon's IsReflectionOnlyClosure(): the closure permits reflection only.
// Malformed or unclassified trees return false.
bool isReflectionOnlyClosure(const mxcpp::SurfaceClosure &closure);

// Typhoon's IsVolumeOnlyBoundary(): a transparent, scattering-free volume
// boundary
bool isVolumeOnlyBoundary(const mxcpp::SurfaceClosure &closure);

// The negation of Typhoon's AllowApproximateTransparentShadowsForNeeOrigin(
// &closure, true): the BSDF has an active coupled thick dielectric
bool hasCoupledThickDielectric(const mxcpp::SurfaceClosure &closure);

} // namespace halcyon::pathtracer
