//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_NODES_PROCEDURAL_HELPERS_H
#define MXCPP_NODES_PROCEDURAL_HELPERS_H

#include "colorHelpers.h"
#include "hashHelper.h"
#include "mathHelpers.h"
#include "valueHelper.h"

#include <renderer/materials/MaterialXCpp/paramMap.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <type_traits>

namespace mxcpp {

// Convert gradient length to a symmetric half-width for smoothstep AA.
inline constexpr float kInvSqrt2 = 0.70710678118654752440f;

inline float
AAStep(float threshold, float value, float dx, float dy)
{
    const float afwidth = std::sqrt(dx * dx + dy * dy) * kInvSqrt2;
    if (afwidth <= 0.0f) {
        return value < threshold ? 0.0f : 1.0f;
    }
    return Smoothstep(threshold - afwidth, threshold + afwidth, value);
}

inline float
GradientFloat(uint32_t hash, float x, float y)
{
    const uint32_t h = hash & 7u;
    const float u = (h < 4u) ? x : y;
    const float v = 2.0f * ((h < 4u) ? y : x);
    return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
}

inline float
GradientFloat(uint32_t hash, float x, float y, float z)
{
    const uint32_t h = hash & 15u;
    const float u = (h < 8u) ? x : y;
    const float v = (h < 4u) ? y : ((h == 12u || h == 14u) ? x : z);
    return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
}

inline Vec3f
GradientVec3(UVec3 hash, float x, float y)
{
    return Vec3f(GradientFloat(hash.x, x, y),
                 GradientFloat(hash.y, x, y),
                 GradientFloat(hash.z, x, y));
}

inline Vec3f
GradientVec3(UVec3 hash, float x, float y, float z)
{
    return Vec3f(GradientFloat(hash.x, x, y, z),
                 GradientFloat(hash.y, x, y, z),
                 GradientFloat(hash.z, x, y, z));
}

inline constexpr float _kGradScale2d = 0.6616f;
inline constexpr float _kGradScale3d = 0.9820f;

inline float
PerlinNoise2d(float px, float py)
{
    int X = 0;
    int Y = 0;
    const float fx = FloorFrac(px, X);
    const float fy = FloorFrac(py, Y);
    const float u = Fade(fx);
    const float v = Fade(fy);
    return _kGradScale2d * Bilerp(
        GradientFloat(HashInt(X,   Y  ), fx,      fy),
        GradientFloat(HashInt(X+1, Y  ), fx-1.0f, fy),
        GradientFloat(HashInt(X,   Y+1), fx,      fy-1.0f),
        GradientFloat(HashInt(X+1, Y+1), fx-1.0f, fy-1.0f),
        u, v);
}

inline Vec3f
PerlinNoise2dVec3(float px, float py)
{
    int X = 0;
    int Y = 0;
    const float fx = FloorFrac(px, X);
    const float fy = FloorFrac(py, Y);
    const float u = Fade(fx);
    const float v = Fade(fy);
    return Bilerp(
        GradientVec3(HashVec3(X,   Y  ), fx,      fy),
        GradientVec3(HashVec3(X+1, Y  ), fx-1.0f, fy),
        GradientVec3(HashVec3(X,   Y+1), fx,      fy-1.0f),
        GradientVec3(HashVec3(X+1, Y+1), fx-1.0f, fy-1.0f),
        u, v) * _kGradScale2d;
}

inline float
PerlinNoise3d(float px, float py, float pz)
{
    int X = 0;
    int Y = 0;
    int Z = 0;
    const float fx = FloorFrac(px, X);
    const float fy = FloorFrac(py, Y);
    const float fz = FloorFrac(pz, Z);
    const float u = Fade(fx);
    const float v = Fade(fy);
    const float w = Fade(fz);
    return _kGradScale3d * Trilerp(
        GradientFloat(HashInt(X,   Y,   Z  ), fx,      fy,      fz),
        GradientFloat(HashInt(X+1, Y,   Z  ), fx-1.0f, fy,      fz),
        GradientFloat(HashInt(X,   Y+1, Z  ), fx,      fy-1.0f, fz),
        GradientFloat(HashInt(X+1, Y+1, Z  ), fx-1.0f, fy-1.0f, fz),
        GradientFloat(HashInt(X,   Y,   Z+1), fx,      fy,      fz-1.0f),
        GradientFloat(HashInt(X+1, Y,   Z+1), fx-1.0f, fy,      fz-1.0f),
        GradientFloat(HashInt(X,   Y+1, Z+1), fx,      fy-1.0f, fz-1.0f),
        GradientFloat(HashInt(X+1, Y+1, Z+1), fx-1.0f, fy-1.0f, fz-1.0f),
        u, v, w);
}

inline Vec3f
PerlinNoise3dVec3(float px, float py, float pz)
{
    int X = 0;
    int Y = 0;
    int Z = 0;
    const float fx = FloorFrac(px, X);
    const float fy = FloorFrac(py, Y);
    const float fz = FloorFrac(pz, Z);
    const float u = Fade(fx);
    const float v = Fade(fy);
    const float w = Fade(fz);
    return Trilerp(
        GradientVec3(HashVec3(X,   Y,   Z  ), fx,      fy,      fz),
        GradientVec3(HashVec3(X+1, Y,   Z  ), fx-1.0f, fy,      fz),
        GradientVec3(HashVec3(X,   Y+1, Z  ), fx,      fy-1.0f, fz),
        GradientVec3(HashVec3(X+1, Y+1, Z  ), fx-1.0f, fy-1.0f, fz),
        GradientVec3(HashVec3(X,   Y,   Z+1), fx,      fy,      fz-1.0f),
        GradientVec3(HashVec3(X+1, Y,   Z+1), fx-1.0f, fy,      fz-1.0f),
        GradientVec3(HashVec3(X,   Y+1, Z+1), fx,      fy-1.0f, fz-1.0f),
        GradientVec3(HashVec3(X+1, Y+1, Z+1), fx-1.0f, fy-1.0f, fz-1.0f),
        u, v, w) * _kGradScale3d;
}

inline float
CellNoise2d(float x, float y)
{
    return BitsTo01(HashInt(static_cast<int>(std::floor(x)),
                            static_cast<int>(std::floor(y))));
}

inline float
CellNoise3d(float x, float y, float z)
{
    return BitsTo01(HashInt(static_cast<int>(std::floor(x)),
                            static_cast<int>(std::floor(y)),
                            static_cast<int>(std::floor(z))));
}

inline float
CellNoise4d(float x, float y, float z, float w)
{
    return BitsTo01(HashInt(static_cast<int>(std::floor(x)),
                            static_cast<int>(std::floor(y)),
                            static_cast<int>(std::floor(z)),
                            static_cast<int>(std::floor(w))));
}

inline Vec3f
CellNoise2dVec3(float x, float y)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    return Vec3f(BitsTo01(HashInt(ix, iy, 0)),
                 BitsTo01(HashInt(ix, iy, 1)),
                 BitsTo01(HashInt(ix, iy, 2)));
}

inline Vec3f
CellNoise3dVec3(float x, float y, float z)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));
    return Vec3f(BitsTo01(HashInt(ix, iy, iz, 0)),
                 BitsTo01(HashInt(ix, iy, iz, 1)),
                 BitsTo01(HashInt(ix, iy, iz, 2)));
}

inline Vec3f
CellNoise4dVec3(float x, float y, float z, float w)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));
    const int iw = static_cast<int>(std::floor(w));
    return Vec3f(BitsTo01(HashInt(ix, iy, iz, iw, 0)),
                 BitsTo01(HashInt(ix, iy, iz, iw, 1)),
                 BitsTo01(HashInt(ix, iy, iz, iw, 2)));
}

inline float
FractalNoise2dFloat(Vec2f p, int octaves, float lacunarity, float diminish)
{
    float result = 0.0f;
    float amplitude = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        result += amplitude * PerlinNoise2d(p[0], p[1]);
        amplitude *= diminish;
        p *= lacunarity;
    }
    return result;
}

inline Vec3f
FractalNoise2dVec3(Vec2f p, int octaves, float lacunarity, float diminish)
{
    Vec3f result(0.0f);
    float amplitude = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        result += PerlinNoise2dVec3(p[0], p[1]) * amplitude;
        amplitude *= diminish;
        p *= lacunarity;
    }
    return result;
}

inline Vec2f
FractalNoise2dVec2(Vec2f p, int octaves, float lacunarity, float diminish)
{
    return Vec2f(
        FractalNoise2dFloat(p, octaves, lacunarity, diminish),
        FractalNoise2dFloat(
            p + Vec2f(19.0f, 193.0f), octaves, lacunarity, diminish));
}

inline Vec4f
FractalNoise2dVec4(Vec2f p, int octaves, float lacunarity, float diminish)
{
    const Vec3f xyz = FractalNoise2dVec3(p, octaves, lacunarity, diminish);
    const float w = FractalNoise2dFloat(
        p + Vec2f(19.0f, 193.0f), octaves, lacunarity, diminish);
    return Vec4f(xyz[0], xyz[1], xyz[2], w);
}

inline float
FractalNoise3dFloat(Vec3f p, int octaves, float lacunarity, float diminish)
{
    float result = 0.0f;
    float amplitude = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        result += amplitude * PerlinNoise3d(p[0], p[1], p[2]);
        amplitude *= diminish;
        p *= lacunarity;
    }
    return result;
}

inline Vec3f
FractalNoise3dVec3(Vec3f p, int octaves, float lacunarity, float diminish)
{
    Vec3f result(0.0f);
    float amplitude = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        result += PerlinNoise3dVec3(p[0], p[1], p[2]) * amplitude;
        amplitude *= diminish;
        p *= lacunarity;
    }
    return result;
}

inline Vec2f
FractalNoise3dVec2(Vec3f p, int octaves, float lacunarity, float diminish)
{
    return Vec2f(
        FractalNoise3dFloat(p, octaves, lacunarity, diminish),
        FractalNoise3dFloat(
            p + Vec3f(19.0f, 193.0f, 17.0f),
            octaves,
            lacunarity,
            diminish));
}

inline Vec4f
FractalNoise3dVec4(Vec3f p, int octaves, float lacunarity, float diminish)
{
    const Vec3f xyz = FractalNoise3dVec3(p, octaves, lacunarity, diminish);
    const float w = FractalNoise3dFloat(
        p + Vec3f(19.0f, 193.0f, 17.0f),
        octaves,
        lacunarity,
        diminish);
    return Vec4f(xyz[0], xyz[1], xyz[2], w);
}

inline Vec2f
WorleyCellPosition2d(int x, int y, int xoff, int yoff, float jitter)
{
    Vec3f tmp = CellNoise2dVec3(float(x + xoff), float(y + yoff));
    Vec2f off(tmp[0], tmp[1]);
    off -= Vec2f(0.5f);
    off *= jitter;
    off += Vec2f(0.5f);
    return Vec2f(float(x), float(y)) + off;
}

inline float
WorleyDistance2d(const Vec2f& p,
                 int x, int y,
                 int xoff, int yoff,
                 float jitter)
{
    const Vec2f cellPos = WorleyCellPosition2d(x, y, xoff, yoff, jitter);
    const Vec2f diff = cellPos - p;
    return Dot(diff, diff);
}

inline float
WorleyNoise2dFloat(const Vec2f& p, float jitter, int style)
{
    int X = 0;
    int Y = 0;
    const Vec2f localPos(FloorFrac(p[0], X), FloorFrac(p[1], Y));
    float minDist = 1.0e6f;
    Vec2f minPos(0.0f);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            const float dist = WorleyDistance2d(localPos, x, y, X, Y, jitter);
            const Vec2f cellPos =
                WorleyCellPosition2d(x, y, X, Y, jitter) - localPos;
            if (dist < minDist) {
                minDist = dist;
                minPos = cellPos;
            }
        }
    }

    if (style == 1) {
        const Vec2f tmpP = minPos + p;
        return CellNoise2d(tmpP[0], tmpP[1]);
    }
    return std::sqrt(minDist);
}

inline Vec2f
WorleyNoise2dVec2(const Vec2f& p, float jitter, int style)
{
    int X = 0;
    int Y = 0;
    const Vec2f localPos(FloorFrac(p[0], X), FloorFrac(p[1], Y));
    Vec2f sqdist(1.0e6f, 1.0e6f);
    Vec2f minPos(0.0f);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            const float dist = WorleyDistance2d(localPos, x, y, X, Y, jitter);
            const Vec2f cellPos =
                WorleyCellPosition2d(x, y, X, Y, jitter) - localPos;
            if (dist < sqdist[0]) {
                sqdist[1] = sqdist[0];
                sqdist[0] = dist;
                minPos = cellPos;
            } else if (dist < sqdist[1]) {
                sqdist[1] = dist;
            }
        }
    }

    if (style == 1) {
        const Vec2f tmpP = minPos + p;
        const Vec3f tmp = CellNoise2dVec3(tmpP[0], tmpP[1]);
        return Vec2f(tmp[0], tmp[1]);
    }
    return Vec2f(std::sqrt(sqdist[0]), std::sqrt(sqdist[1]));
}

inline Vec3f
WorleyNoise2dVec3(const Vec2f& p, float jitter, int style)
{
    int X = 0;
    int Y = 0;
    const Vec2f localPos(FloorFrac(p[0], X), FloorFrac(p[1], Y));
    Vec3f sqdist(1.0e6f, 1.0e6f, 1.0e6f);
    Vec2f minPos(0.0f);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            const float dist = WorleyDistance2d(localPos, x, y, X, Y, jitter);
            const Vec2f cellPos =
                WorleyCellPosition2d(x, y, X, Y, jitter) - localPos;
            if (dist < sqdist[0]) {
                sqdist[2] = sqdist[1];
                sqdist[1] = sqdist[0];
                sqdist[0] = dist;
                minPos = cellPos;
            } else if (dist < sqdist[1]) {
                sqdist[2] = sqdist[1];
                sqdist[1] = dist;
            } else if (dist < sqdist[2]) {
                sqdist[2] = dist;
            }
        }
    }

    if (style == 1) {
        const Vec2f tmpP = minPos + p;
        return CellNoise2dVec3(tmpP[0], tmpP[1]);
    }
    return Vec3f(std::sqrt(sqdist[0]),
                 std::sqrt(sqdist[1]),
                 std::sqrt(sqdist[2]));
}

inline Vec3f
WorleyCellPosition3d(int x, int y, int z,
                     int xoff, int yoff, int zoff,
                     float jitter)
{
    Vec3f off = CellNoise3dVec3(float(x + xoff),
                                float(y + yoff),
                                float(z + zoff));
    off -= Vec3f(0.5f);
    off *= jitter;
    off += Vec3f(0.5f);
    return Vec3f(float(x), float(y), float(z)) + off;
}

inline float
WorleyDistance3d(const Vec3f& p,
                 int x, int y, int z,
                 int xoff, int yoff, int zoff,
                 float jitter)
{
    const Vec3f cellPos =
        WorleyCellPosition3d(x, y, z, xoff, yoff, zoff, jitter);
    const Vec3f diff = cellPos - p;
    return Dot(diff, diff);
}

inline float
WorleyNoise3dFloat(const Vec3f& p, float jitter, int style)
{
    int X = 0;
    int Y = 0;
    int Z = 0;
    const Vec3f localPos(FloorFrac(p[0], X),
                         FloorFrac(p[1], Y),
                         FloorFrac(p[2], Z));
    float minDist = 1.0e6f;
    Vec3f minPos(0.0f);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                const float dist =
                    WorleyDistance3d(localPos, x, y, z, X, Y, Z, jitter);
                const Vec3f cellPos =
                    WorleyCellPosition3d(x, y, z, X, Y, Z, jitter) - localPos;
                if (dist < minDist) {
                    minDist = dist;
                    minPos = cellPos;
                }
            }
        }
    }

    if (style == 1) {
        const Vec3f tmpP = minPos + p;
        return CellNoise3d(tmpP[0], tmpP[1], tmpP[2]);
    }
    return std::sqrt(minDist);
}

inline Vec2f
WorleyNoise3dVec2(const Vec3f& p, float jitter, int style)
{
    int X = 0;
    int Y = 0;
    int Z = 0;
    const Vec3f localPos(FloorFrac(p[0], X),
                         FloorFrac(p[1], Y),
                         FloorFrac(p[2], Z));
    Vec2f sqdist(1.0e6f, 1.0e6f);
    Vec3f minPos(0.0f);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                const float dist =
                    WorleyDistance3d(localPos, x, y, z, X, Y, Z, jitter);
                const Vec3f cellPos =
                    WorleyCellPosition3d(x, y, z, X, Y, Z, jitter) - localPos;
                if (dist < sqdist[0]) {
                    sqdist[1] = sqdist[0];
                    sqdist[0] = dist;
                    minPos = cellPos;
                } else if (dist < sqdist[1]) {
                    sqdist[1] = dist;
                }
            }
        }
    }

    if (style == 1) {
        const Vec3f tmpP = minPos + p;
        const Vec3f tmp = CellNoise3dVec3(tmpP[0], tmpP[1], tmpP[2]);
        return Vec2f(tmp[0], tmp[1]);
    }
    return Vec2f(std::sqrt(sqdist[0]), std::sqrt(sqdist[1]));
}

inline Vec3f
WorleyNoise3dVec3(const Vec3f& p, float jitter, int style)
{
    int X = 0;
    int Y = 0;
    int Z = 0;
    const Vec3f localPos(FloorFrac(p[0], X),
                         FloorFrac(p[1], Y),
                         FloorFrac(p[2], Z));
    Vec3f sqdist(1.0e6f, 1.0e6f, 1.0e6f);
    Vec3f minPos(0.0f);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                const float dist =
                    WorleyDistance3d(localPos, x, y, z, X, Y, Z, jitter);
                const Vec3f cellPos =
                    WorleyCellPosition3d(x, y, z, X, Y, Z, jitter) - localPos;
                if (dist < sqdist[0]) {
                    sqdist[2] = sqdist[1];
                    sqdist[1] = sqdist[0];
                    sqdist[0] = dist;
                    minPos = cellPos;
                } else if (dist < sqdist[1]) {
                    sqdist[2] = sqdist[1];
                    sqdist[1] = dist;
                } else if (dist < sqdist[2]) {
                    sqdist[2] = dist;
                }
            }
        }
    }

    if (style == 1) {
        const Vec3f tmpP = minPos + p;
        return CellNoise3dVec3(tmpP[0], tmpP[1], tmpP[2]);
    }
    return Vec3f(std::sqrt(sqdist[0]),
                 std::sqrt(sqdist[1]),
                 std::sqrt(sqdist[2]));
}

inline Vec3f
RotateFlake(const Vec3f& p, const Vec3f& i)
{
    const float theta = kTwoPi * i[0];
    const float phi = kTwoPi * i[1];
    const float z = i[2] * 2.0f;

    const float r = std::sqrt(z);
    const float vx = std::sin(phi) * r;
    const float vy = std::cos(phi) * r;
    const float vz = std::sqrt(2.0f - z);

    const float sTheta = std::sin(theta);
    const float cTheta = std::cos(theta);
    const float sx = vx * cTheta - vy * sTheta;
    const float sy = vx * sTheta + vy * cTheta;

    return Vec3f(
        (vx * sx - cTheta) * p[0] + (vy * sx + sTheta) * p[1] +
            vz * sx * p[2],
        (vx * sy - sTheta) * p[0] + (vy * sy - cTheta) * p[1] +
            vz * sy * p[2],
        vx * vz * p[0] + vy * vz * p[1] + (1.0f - z) * p[2]);
}

inline float
FlakeDensityToProbability(float x)
{
    const Vec4f abcd(-26.19771808f, 26.39663835f,
                     85.53857017f, -102.35069432f);
    const Vec2f ef(-101.42634862f, 118.45082288f);
    const float xx = x * x;
    return (abcd[0] * xx + abcd[1] * x) /
           (abcd[2] * xx * x + abcd[3] * xx + ef[0] * x + ef[1]);
}

inline void
EvalFlake(const Vec3f& position,
          float size,
          float roughness,
          float coverage,
          const Vec3f& normal,
          const Vec3f& tangent,
          const Vec3f& bitangent,
          int* id,
          float* rand,
          float* presence,
          Vec3f* flakeNormal)
{
    const float probability = FlakeDensityToProbability(Clamp01(coverage));
    const float flakeDiameter = 1.5f / std::sqrt(3.0f);

    const Vec3f p = position / Vec3f(size);
    const Vec3f baseP(std::floor(p[0]), std::floor(p[1]), std::floor(p[2]));

    float flakePriority = 0.0f;
    Vec3f flakeCell(0.0f);

    for (int i = -1; i < 2; ++i) {
        for (int j = -1; j < 2; ++j) {
            for (int k = -1; k < 2; ++k) {
                const Vec3f cellPos =
                    baseP + Vec3f(float(i), float(j), float(k));

                Vec3f pp = p - cellPos - Vec3f(0.5f);
                if (Dot(pp, pp) >=
                    flakeDiameter * flakeDiameter * 3.0f) {
                    continue;
                }

                if (CellNoise3d(cellPos[0], cellPos[1], cellPos[2]) >
                    probability) {
                    continue;
                }

                const float priority = CellNoise4d(
                    cellPos[0], cellPos[1], cellPos[2], 3.0f);
                if (priority < flakePriority) {
                    continue;
                }

                pp = RotateFlake(
                    pp,
                    CellNoise3dVec3(cellPos[0], cellPos[1], cellPos[2]));

                if (std::fabs(pp[0]) <= flakeDiameter &&
                    std::fabs(pp[1]) <= flakeDiameter &&
                    std::fabs(pp[2]) <= flakeDiameter) {
                    flakePriority = priority;
                    flakeCell = cellPos;
                }
            }
        }
    }

    if (flakePriority <= 0.0f) {
        if (id) *id = 0;
        if (rand) *rand = 0.0f;
        if (presence) *presence = 0.0f;
        if (flakeNormal) *flakeNormal = normal;
        return;
    }

    const Vec3f flakeNoise = CellNoise4dVec3(
        flakeCell[0], flakeCell[1], flakeCell[2], 2.0f);
    const float xi0 = flakeNoise[0];
    const float xi1 = flakeNoise[1];

    if (id) {
        *id = static_cast<int>(flakeNoise[2] * 16777215.0f);
    }
    if (rand) {
        *rand = flakeNoise[2];
    }
    if (presence) {
        *presence = flakePriority;
    }

    const float phi = kTwoPi * xi0;
    const float tanTheta = roughness * roughness *
                           std::sqrt(xi1) / std::sqrt(1.0f - xi1);
    const float sinTheta =
        tanTheta / std::sqrt(1.0f + tanTheta * tanTheta);
    const float cosTheta = std::sqrt(1.0f - sinTheta * sinTheta);

    Vec3f n = tangent * std::cos(phi) * sinTheta +
              bitangent * std::sin(phi) * sinTheta +
              normal * cosTheta;
    n.normalize();
    if (flakeNormal) {
        *flakeNormal = n;
    }
}

template<typename T>
inline T
ReadAmplitude(const ParamMap& inputs, const SlotName& amplitudeSlot)
{
    return Get<T>(inputs, amplitudeSlot, One<T>());
}

template<>
inline Vec2f
ReadAmplitude<Vec2f>(const ParamMap& inputs, const SlotName& amplitudeSlot)
{
    if (const Value* value = inputs.Find(amplitudeSlot)) {
        if (ValueHolds<Vec2f>(*value)) {
            return ValueGet<Vec2f>(*value);
        }
        if (ValueHolds<float>(*value)) {
            return Vec2f(ValueGet<float>(*value));
        }
    }
    return Vec2f(1.0f);
}

template<>
inline Vec3f
ReadAmplitude<Vec3f>(const ParamMap& inputs, const SlotName& amplitudeSlot)
{
    if (const Value* value = inputs.Find(amplitudeSlot)) {
        if (ValueHolds<Vec3f>(*value)) {
            return ValueGet<Vec3f>(*value);
        }
        if (ValueHolds<float>(*value)) {
            return Vec3f(ValueGet<float>(*value));
        }
    }
    return Vec3f(1.0f);
}

template<>
inline Vec4f
ReadAmplitude<Vec4f>(const ParamMap& inputs, const SlotName& amplitudeSlot)
{
    if (const Value* value = inputs.Find(amplitudeSlot)) {
        if (ValueHolds<Vec4f>(*value)) {
            return ValueGet<Vec4f>(*value);
        }
        if (ValueHolds<float>(*value)) {
            return Vec4f(ValueGet<float>(*value));
        }
    }
    return Vec4f(1.0f);
}

template<typename T>
inline void
StoreTypedOutput(NodeOutputMap* outputs, const SlotName& slot, const T& value)
{
    (*outputs)[slot] = Value(value);
}

inline float
RandomFloatValue(float inputValue, float minValue, float maxValue, int seed)
{
    const float noise = CellNoise2d(inputValue, static_cast<float>(seed));
    return ClampValue(Remap(noise, 0.0f, 1.0f, minValue, maxValue),
                      std::min(minValue, maxValue),
                      std::max(minValue, maxValue));
}

}  // namespace mxcpp

#endif
