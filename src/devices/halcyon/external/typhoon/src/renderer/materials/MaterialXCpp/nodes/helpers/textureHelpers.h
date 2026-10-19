//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_NODES_TEXTURE_HELPERS_H
#define MXCPP_NODES_TEXTURE_HELPERS_H

#include "colorHelpers.h"
#include "inputEvaluationHelpers.h"
#include "mathHelpers.h"
#include "shadingContextHelpers.h"
#include "spaceHelpers.h"

#include <renderer/materials/MaterialXCpp/paramMap.h>

#include <algorithm>
#include <string>
#include <type_traits>

namespace mxcpp {

static constexpr float _kFloatEps = 1e-6f;

inline Vec3f
GetDefaultObjectNormal(const ShadingContext& ctx)
{
    Vec3f objectNormal = ctx.normal;
    TransformNamedVec3(
        ctx, "world", "object",
        ShadingContext::TransformSpaceType::Normal,
        ctx.normal, &objectNormal);
    return objectNormal;
}

inline std::string
NormalizeToken(const std::string& value, const char* fallback)
{
    return NormalizeSpaceName(value, std::string(fallback));
}

template<typename T>
struct TextureValueTraits;

template<>
struct TextureValueTraits<float>
{
    static constexpr int kChannelCount = 1;
    static constexpr float kFillValue = 0.0f;

    static Vec4f ToVec4(float value)
    {
        return Vec4f(value, 0.0f, 0.0f, 0.0f);
    }

    static float FromVec4(const Vec4f& value)
    {
        return value[0];
    }
};

template<>
struct TextureValueTraits<Vec2f>
{
    static constexpr int kChannelCount = 2;
    static constexpr float kFillValue = 0.0f;

    static Vec4f ToVec4(const Vec2f& value)
    {
        return Vec4f(value[0], value[1], 0.0f, 0.0f);
    }

    static Vec2f FromVec4(const Vec4f& value)
    {
        return Vec2f(value[0], value[1]);
    }
};

template<>
struct TextureValueTraits<Vec3f>
{
    static constexpr int kChannelCount = 3;
    static constexpr float kFillValue = 0.0f;

    static Vec4f ToVec4(const Vec3f& value)
    {
        return Vec4f(value[0], value[1], value[2], 0.0f);
    }

    static Vec3f FromVec4(const Vec4f& value)
    {
        return Vec3f(value[0], value[1], value[2]);
    }
};

template<>
struct TextureValueTraits<Vec4f>
{
    static constexpr int kChannelCount = 4;

    static Vec4f ToVec4(const Vec4f& value)
    {
        return value;
    }

    static Vec4f FromVec4(const Vec4f& value)
    {
        return value;
    }
};

inline TextureAddressMode
GetAddressMode(const ParamMap& inputs,
               const SlotName& slot,
               const char* fallback)
{
    const std::string mode =
        NormalizeToken(Get<std::string>(inputs, slot, std::string(fallback)),
                       fallback);
    if (mode == "constant") {
        return TextureAddressMode::Constant;
    }
    if (mode == "clamp") {
        return TextureAddressMode::Clamp;
    }
    if (mode == "mirror") {
        return TextureAddressMode::Mirror;
    }
    return TextureAddressMode::Periodic;
}

inline TextureAddressMode
GetUsdAddressMode(const ParamMap& inputs,
                  const SlotName& slot,
                  const char* fallback)
{
    const std::string mode =
        NormalizeToken(Get<std::string>(inputs, slot, std::string(fallback)),
                       fallback);
    if (mode == "black" || mode == "constant") {
        return TextureAddressMode::Constant;
    }
    if (mode == "clamp") {
        return TextureAddressMode::Clamp;
    }
    if (mode == "mirror") {
        return TextureAddressMode::Mirror;
    }
    if (mode == "usemetadata") {
        return TextureAddressMode::UseMetadata;
    }
    return TextureAddressMode::Periodic;
}

inline TextureFilterType
GetFilterType(const ParamMap& inputs, const SlotName& slot)
{
    const std::string filterType =
        NormalizeToken(
            Get<std::string>(inputs, slot, std::string("linear")),
            "linear");
    if (filterType == "closest") {
        return TextureFilterType::Closest;
    }
    if (filterType == "cubic") {
        return TextureFilterType::Cubic;
    }
    return TextureFilterType::Linear;
}

inline bool
UsesConstantDefaultOutside(const TextureAddressMode mode, const float value)
{
    return mode == TextureAddressMode::Constant &&
           (value < 0.0f || value > 1.0f);
}

inline void
ApplyContextTextureBlur(const ShadingContext& ctx, Texture2DRequest* request)
{
    if (!request) {
        return;
    }

    request->blur[0] = std::max(request->blur[0], ctx.textureBlur[0]);
    request->blur[1] = std::max(request->blur[1], ctx.textureBlur[1]);
}

}  // namespace mxcpp

#endif
