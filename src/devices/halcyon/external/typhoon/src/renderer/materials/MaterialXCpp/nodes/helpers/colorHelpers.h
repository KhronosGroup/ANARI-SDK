//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_NODES_COLOR_HELPERS_H
#define MXCPP_NODES_COLOR_HELPERS_H

#include <renderer/materials/MaterialXCpp/mathTypes.h>

#include <algorithm>
#include <cmath>

namespace mxcpp {

constexpr float kRec709LumaR = 0.2126f;
constexpr float kRec709LumaG = 0.7152f;
constexpr float kRec709LumaB = 0.0722f;

constexpr float kAcesCgLumaR = 0.2722287f;
constexpr float kAcesCgLumaG = 0.6740818f;
constexpr float kAcesCgLumaB = 0.0536895f;

inline Vec3f
Rec709LumaCoeffs()
{
    return Vec3f(kRec709LumaR, kRec709LumaG, kRec709LumaB);
}

inline Vec3f
AcesCgLumaCoeffs()
{
    return Vec3f(kAcesCgLumaR, kAcesCgLumaG, kAcesCgLumaB);
}

inline Vec3f
RgbToHsv(const Vec3f& rgb)
{
    const float r = rgb[0];
    const float g = rgb[1];
    const float b = rgb[2];
    const float cmax = std::max({r, g, b});
    const float cmin = std::min({r, g, b});
    const float delta = cmax - cmin;

    float h = 0.0f;
    if (delta > 0.0f) {
        if (cmax == r) {
            h = std::fmod((g - b) / delta, 6.0f);
        } else if (cmax == g) {
            h = (b - r) / delta + 2.0f;
        } else {
            h = (r - g) / delta + 4.0f;
        }
        h /= 6.0f;
        if (h < 0.0f) {
            h += 1.0f;
        }
    }

    const float s = cmax > 0.0f ? delta / cmax : 0.0f;
    return Vec3f(h, s, cmax);
}

inline Vec3f
HsvToRgb(const Vec3f& hsv)
{
    const float h = hsv[0];
    const float s = hsv[1];
    const float v = hsv[2];
    const float c = v * s;
    const float hh = h * 6.0f;
    const float x = c * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f));
    const float m = v - c;

    Vec3f rgb;
    if (hh < 1.0f) {
        rgb = Vec3f(c, x, 0.0f);
    } else if (hh < 2.0f) {
        rgb = Vec3f(x, c, 0.0f);
    } else if (hh < 3.0f) {
        rgb = Vec3f(0.0f, c, x);
    } else if (hh < 4.0f) {
        rgb = Vec3f(0.0f, x, c);
    } else if (hh < 5.0f) {
        rgb = Vec3f(x, 0.0f, c);
    } else {
        rgb = Vec3f(c, 0.0f, x);
    }

    return rgb + Vec3f(m);
}

}  // namespace mxcpp

#endif
