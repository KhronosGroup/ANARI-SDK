// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The colour management the pathtracer's materials need: Typhoon's
// renderer/colorManagement.* for its default working space, linear Rec. 709
// (Typhoon's RenderColorSpace::LinearRec709), without OpenUSD. Typhoon
// converts authored colours that name a colour space, texture colours and
// light colours into the working space with it, and takes its luminance
// coefficients from it. See ColorManagement.cpp for where it comes from.

// typhoon
#include <renderer/materials/MaterialXCpp/mathTypes.h>
// std
#include <string>

namespace halcyon::pathtracer {

// The working space's name (OpenUSD's GfColorSpaceNames->LinearRec709)
extern const char *const WORKING_COLOR_SPACE;

// Converts 'rgb' from colour space 'source' (an OpenUSD GfColorSpace name, or
// its descriptive name) into the working space (Typhoon's
// ConvertToRenderColorSpace() with RenderColorSpace::LinearRec709). "data",
// "raw", "identity", "unknown" and the working space itself leave 'rgb' as it
// is. Returns false, leaving 'rgb' unchanged, if 'source' isn't a colour space
// OpenUSD knows; names are taken as they are, no aliases (so MaterialX's
// "srgb_texture" or "lin_rec709" aren't known).
bool convertToWorkingSpace(const std::string &source, mxcpp::Vec3f &rgb);

// The working space's luminance coefficients: CIE Y of its RGB-to-XYZ matrix
// (Typhoon's GetLuminanceCoefficients(RenderColorSpace::LinearRec709)), which
// the shading context passes to the materials
mxcpp::Vec3f luminanceCoefficients();

// The shading context's colour transform callback
// (mxcpp::ShadingContext::ColorTransformFn): converts 'in' from 'source' into
// 'target' when 'target' is the working space and convertToWorkingSpace()
// knows 'source'; otherwise declines (returns false), and MaterialXCpp's
// colortransform nodes use their own conversions. Those nodes name MaterialX
// colour spaces ("g22_rec709" to "lin_rec709", ...), which OpenUSD doesn't
// know, so they always use their own, as in Typhoon, which installs no
// callback. 'userData' isn't read.
bool colorTransform(const void *userData,
    const std::string &source,
    const std::string &target,
    const mxcpp::Vec3f &in,
    mxcpp::Vec3f *out);

} // namespace halcyon::pathtracer
