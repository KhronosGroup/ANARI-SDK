//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_NODES_SPACE_HELPERS_H
#define MXCPP_NODES_SPACE_HELPERS_H

#include "mathHelpers.h"

#include <renderer/materials/MaterialXCpp/shadingContext.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace mxcpp {

inline std::string
NormalizeSpaceName(const std::string& space, const std::string& workingSpace)
{
    std::string normalized = space.empty() ? workingSpace : space;
    std::transform(
        normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalized == "model") {
        return "object";
    }
    if (normalized == "pobject" ||
        normalized == "nobject" ||
        normalized == "tobject" ||
        normalized == "bobject") {
        return "object";
    }
    if (normalized == "pworld" ||
        normalized == "nworld" ||
        normalized == "tworld" ||
        normalized == "bworld") {
        return "world";
    }
    return normalized;
}

inline bool
TransformNamedVec3(const ShadingContext& ctx,
                   const std::string& fromSpace,
                   const std::string& toSpace,
                   ShadingContext::TransformSpaceType type,
                   const Vec3f& in,
                   Vec3f* out)
{
    const std::string from = NormalizeSpaceName(fromSpace, ctx.workingSpace);
    const std::string to = NormalizeSpaceName(toSpace, ctx.workingSpace);

    if (from == to) {
        if (out) {
            *out = in;
        }
        return true;
    }

    if (ctx.transformSpace &&
        ctx.transformSpace(ctx.transformUserData, from, to, type, in, out)) {
        return true;
    }

    const bool fromObject = from == "object";
    const bool fromWorld = from == "world";
    const bool toObject = to == "object";
    const bool toWorld = to == "world";
    if ((!fromObject && !fromWorld) || (!toObject && !toWorld)) {
        return false;
    }

    Vec3f result(0.0f);
    if (type == ShadingContext::TransformSpaceType::Point) {
        if (fromObject && toWorld && ctx.hasObjectToWorldTransform) {
            ctx.objectToWorldMatrix.multVecMatrix(in, result);
        } else if (fromWorld && toObject && ctx.hasWorldToObjectTransform) {
            ctx.worldToObjectMatrix.multVecMatrix(in, result);
        } else {
            return false;
        }
    } else if (type == ShadingContext::TransformSpaceType::Vector) {
        if (fromObject && toWorld && ctx.hasObjectToWorldTransform) {
            ctx.objectToWorldMatrix.multDirMatrix(in, result);
        } else if (fromWorld && toObject && ctx.hasWorldToObjectTransform) {
            ctx.worldToObjectMatrix.multDirMatrix(in, result);
        } else {
            return false;
        }
    } else {
        if (fromObject && toWorld && ctx.hasWorldToObjectTransform) {
            ctx.worldToObjectMatrix.transposed().multDirMatrix(in, result);
        } else if (fromWorld && toObject && ctx.hasObjectToWorldTransform) {
            ctx.objectToWorldMatrix.transposed().multDirMatrix(in, result);
        } else {
            return false;
        }
    }

    if (out) {
        *out = result;
    }
    return true;
}

}  // namespace mxcpp

#endif
