// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: MIT
//
// The 'cortao' ambient occlusion estimator: CORTAO (Circular Occluder
// Ray-Traced Ambient Occlusion), a screen-space estimator that approximates
// each pixel's neighbourhood by circular occluders built from the normal
// target and tests a fixed set of hemisphere rays against them. A port of
// halcyon's preview renderer (src/devices/halcyon/renderer/preview/
// Cortao.isph, cortaoOcclusionAt()), which documents its departures from the
// reference and the reasoning behind its constants (halcyon's ADR 0007).
//
// Ported from CORTAO (github.com/theagentd/CORTAO at 420b168d, reference
// implementation v1.0.3: src/shaders/CORTAOMainPass.h's cortao_mainPass(),
// cortao_computeRayOcclusion(), cortao_rayDirection() and
// cortao_resolveOcclusion(), and src/shaders/CORTAOPrepass.h's
// cortao_prepass() mip chain), under the MIT License:
//
//   MIT License
//
//   Copyright (c) 2026 Daniel Isheden
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit
//   persons to whom the Software is furnished to do so, subject to the
//   following conditions:
//
//   The above copyright notice and this permission notice shall be included
//   in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
//   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
//   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
//   USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Differences from halcyon's port, which works in world space on the CPU:
// - It works in view space, as ssao.frag does, projecting the sampling disk
//   with the projection's x and y scale.
// - It has no stored mip chain. halcyon's hit buffer pyramid point-decimates
//   the hit buffer, each texel of level k holding the surface of one pixel
//   of level 0, so a tap of level k reads that pixel of the normal target
//   directly (cortaoLevelPixel()) and gets the surface halcyon's texel
//   holds, at full precision rather than with an octahedral normal. The cost
//   is cache locality: a stored chain would put a coarse level's taps next to
//   each other in memory.
// - The normals are made to face the viewer here, as in ssao.frag, where
//   halcyon's hit buffer stores them facing the viewer.
//
// Requires AO_ROTATIONS and AO_TWO_PI from ssao.frag, which includes this
// file after them.

// Hemisphere rays per pixel (CORTAO_RAY_COUNT, 8 to 32: the bits of a uint;
// the reference's default)
const int CORTAO_RAYS = 16;
// pi (3 - sqrt(5))
const float CORTAO_GOLDEN_ANGLE = 2.3999632297286533;
// How strongly the samples' radii are drawn towards the pixel
const float CORTAO_SKEW = 3.5;
// The reference's configuration defaults: the largest sampling radius on
// screen as a fraction of the image height (perspective cameras only), the
// largest slope compensation, and the backface heuristic's threshold and
// boost
const float CORTAO_MAX_SCREEN_RADIUS_HEIGHT_RATIO = 0.2;
const float CORTAO_MAX_SLOPE_COMPENSATION = 4.0;
const float CORTAO_BACKFACE_THRESHOLD = 0.5;
const float CORTAO_BACKFACE_BOOST = 0.0;
// How far a front-facing occluder moves into its surface, in occluder radii:
// the reference moves it 1, which leaves contacts too light (halcyon's
// Cortao.isph)
const float CORTAO_OCCLUDER_OFFSET_SCALE = 0.75;
// Most mip levels, the finest included (the reference prepass's 8)
const int CORTAO_MAX_LEVELS = 8;
// Added to the log2 of a sample strip's outer radius in pixels to give its
// mip level (the reference's default 'mipBias'): a strip reads a level whose
// texels are about 1/16 of its radius
const float CORTAO_MIP_BIAS = -4.0;

// The mip levels for a frame of 'size' pixels: down to a level 1 pixel high
// or wide
int cortaoLevels(ivec2 size)
{
    int levels = 1;
    while (levels < CORTAO_MAX_LEVELS && (size.x >> levels) > 0
        && (size.y >> levels) > 0)
        levels++;
    return levels;
}

// The pixel of level 0 whose surface texel 'texel' of mip level 'level'
// holds: of the 2x2 block under texel (X, Y), the pixel whose x parity is
// Y's and whose y parity is X's, as the reference prepass picks it with its
// random seed 0 (halcyon's cortaoWritePyramid())
ivec2 cortaoLevelPixel(ivec2 texel, int level)
{
    for (int k = level; k > 0; --k)
        texel = ivec2(2 * texel.x + (texel.y & 1), 2 * texel.y + (texel.x & 1));
    return texel;
}

// Unit vectors 't' and 'b' completing the unit vector 'n' to an orthonormal
// basis (Duff et al., "Building an Orthonormal Basis, Revisited", JCGT 2017)
void cortaoBasis(vec3 n, out vec3 t, out vec3 b)
{
    float sign = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + sign * n.x * n.x * a, sign * c, -sign * n.x);
    b = vec3(c, sign + n.y * n.y * a, -n.y);
}

// CORTAO's occlusion factor (not raised to the intensity) of the surface at
// 'pixel', at view-space position 'P' with unit normal 'n' facing the viewer,
// for occlusion radius 'radius' (> 0) and 'samples' samples: 1 minus the
// fraction of CORTAO_RAYS hemisphere rays that some occluder blocks.
// 'frameSize', 'projScale' and 'perspective' are ssao.frag's uniforms, 'view'
// its view matrix.
float cortaoOcclusion(sampler2D normalTex,
    ivec2 pixel,
    vec3 P,
    vec3 n,
    float radius,
    int samples,
    vec2 frameSize,
    vec2 projScale,
    bool perspective,
    mat4 view)
{
    ivec2 size = ivec2(frameSize);
    // pixels per unit at unit view depth
    vec2 scale = 0.5 * frameSize * projScale;

    // Half a pixel's diagonal at unit depth: how far the pixel's position is
    // pushed along its normal, per unit of view depth, so it doesn't occlude
    // itself (the reference's 'normalBias')
    float normalBias = 0.70710678 / scale.x;

    // V: towards the eye; the sampling disk lies across it
    vec3 V;
    float clampedRadius = radius;
    float circleRadius = radius;
    // the mip level of a strip of outer radius 1 (of the disk's radius)
    float baseMip;
    if (perspective) {
        float viewDepth = -P.z;
        float distance = length(P);
        V = -P / distance;
        // the radius is clamped so the sampling sphere covers at most
        // CORTAO_MAX_SCREEN_RADIUS_HEIGHT_RATIO of the image height on
        // screen ('maxApparentCircleRadiusCoefficient'), which also keeps it
        // inside the eye's distance
        float maxApparent =
            CORTAO_MAX_SCREEN_RADIUS_HEIGHT_RATIO * frameSize.y / scale.y;
        float coefficient = maxApparent / sqrt(maxApparent * maxApparent + 1.0);
        clampedRadius = min(radius, viewDepth * coefficient);
        // the disk across V through P that the sphere's silhouette cone cuts
        circleRadius = clampedRadius * distance
            * inversesqrt(distance * distance - clampedRadius * clampedRadius);
        // the disk's radius in pixels, stretched off the view axis as the
        // reference stretches it ('stretchMultiplier')
        float stretch = inversesqrt(viewDepth / distance);
        baseMip = log2(circleRadius / distance * scale.y * stretch)
            + CORTAO_MIP_BIAS;
        P += n * (normalBias * viewDepth);
    } else {
        V = vec3(0.0, 0.0, 1.0);
        baseMip = log2(radius * scale.y) + CORTAO_MIP_BIAS;
        P += n * normalBias;
    }

    // The pixel's cell of the 4x4 pattern picks its random values, each a
    // permutation of the 16 steps over the cell
    int cell = (pixel.y & 3) * 4 + (pixel.x & 3);
    int transposed = (pixel.x & 3) * 4 + (pixel.y & 3);
    float sampleRotation = float(AO_ROTATIONS[cell]) * (AO_TWO_PI / 16.0);
    float offsetDither = (float(AO_ROTATIONS[transposed]) + 0.5) / 16.0;
    float tangentRotation =
        float(AO_ROTATIONS[(cell + 5) & 15]) * (AO_TWO_PI / 16.0);
    bool flipSpiral = (AO_ROTATIONS[(cell + 10) & 15] & 1) != 0;

    // the sampling disk's axes, rotated by the pixel's sample rotation (the
    // reference's 'rotationScaleMatrix'), the spiral randomly turning either
    // way
    vec3 right0, up0;
    cortaoBasis(V, right0, up0);
    if (flipSpiral)
        right0 = -right0;
    float sr = sin(sampleRotation);
    float cr = cos(sampleRotation);
    vec3 right = cr * right0 + sr * up0;
    vec3 up = cr * up0 - sr * right0;

    // the rays' tangent frame, randomly rotated about the normal
    vec3 t0, b0;
    cortaoBasis(n, t0, b0);
    float s = sin(tangentRotation);
    float co = cos(tangentRotation);
    vec3 tangent = co * t0 + s * b0;
    vec3 bitangent = co * b0 - s * t0;

    // cortao_rayDirection(): CORTAO_RAYS directions on a golden-angle spiral
    // over the hemisphere, cosine distributed
    vec3 rays[CORTAO_RAYS];
    for (int j = 0; j < CORTAO_RAYS; j++) {
        float angle = float(j) * CORTAO_GOLDEN_ANGLE;
        float rr = sqrt((float(j) + 0.5) / float(CORTAO_RAYS));
        rays[j] = vec3(cos(angle) * rr, sin(angle) * rr, sqrt(1.0 - rr * rr));
    }

    // Pixel coordinates of the disk point P + a right + b up are
    // ((P + a right + b up).xy / z * scale + frameSize / 2), z being its view
    // depth for a perspective camera and 1 for an orthographic one: linear in
    // a and b but for the perspective division (the reference's
    // 'offsetTransform')
    vec2 halfSize = 0.5 * frameSize;
    int levels = cortaoLevels(size);
    mat3 normalToView = mat3(view);

    float inverseMaxSlopeCompensation = 1.0 / CORTAO_MAX_SLOPE_COMPENSATION;
    uint rayBits = 0u;
    for (int i = 0; i < samples; i++) {
        // the sample's strip of the disk: a golden-angle spiral whose radii
        // are drawn towards the centre; 'sampleWeight' is the square root of
        // the strip's share of the disk's area
        float inner = pow(float(i) / float(samples), CORTAO_SKEW * 0.5);
        float outer = pow(float(i + 1) / float(samples), CORTAO_SKEW * 0.5);
        float sampleWeight = sqrt(outer * outer - inner * inner);
        float angle = float(i) * CORTAO_GOLDEN_ANGLE;
        float offset = circleRadius * (inner + (outer - inner) * offsetDither);
        vec3 Q = P + (offset * cos(angle)) * right + (offset * sin(angle)) * up;
        float qz = perspective ? -Q.z : 1.0;
        if (!(qz > 0.0))
            continue;
        vec2 p = Q.xy / qz * scale + halfSize;
        if (!(p.x >= 0.0 && p.x < frameSize.x && p.y >= 0.0
                && p.y < frameSize.y))
            continue;
        int level = clamp(
            int(floor(baseMip + log2(outer) + 0.5)), 0, levels - 1);
        ivec2 texel = ivec2(p) >> level;
        if (texel.x >= max(size.x >> level, 1)
            || texel.y >= max(size.y >> level, 1))
            continue;
        ivec2 tap = cortaoLevelPixel(texel, level);
        vec4 tapNormalDepth = texelFetch(normalTex, tap, 0);
        if (!(tapNormalDepth.w > 0.0))
            continue;
        vec3 samplePosition = viewPosition(tap, tapNormalDepth.w);
        vec3 dPos = samplePosition - P;

        // only samples inside the hemisphere of radius 'clampedRadius'
        // occlude
        float lengthSquared = dot(dPos, dPos);
        if (!(lengthSquared < clampedRadius * clampedRadius
                && dot(dPos, n) > 0.0))
            continue;
        float inverseLength = inversesqrt(lengthSquared);
        vec3 direction = dPos * inverseLength;
        // the sample's normal, facing the viewer
        vec3 sampleNormal = normalize(normalToView * tapNormalDepth.xyz);
        vec3 sampleToViewer =
            perspective ? -samplePosition : vec3(0.0, 0.0, 1.0);
        if (dot(sampleNormal, sampleToViewer) < 0.0)
            sampleNormal = -sampleNormal;

        // the occluder's radius over its distance, larger for surfaces seen
        // at a grazing angle (slope compensation)
        float slopeCompensation = inversesqrt(
            max(dot(sampleNormal, V), inverseMaxSlopeCompensation));
        float occluderRadius = (clampedRadius * sampleWeight)
            * (slopeCompensation * inverseLength);
        if (dot(sampleNormal, direction) < CORTAO_BACKFACE_THRESHOLD) {
            // not a backface: the occluder moves into its surface,
            // CORTAO_OCCLUDER_OFFSET_SCALE radii
            float adjustedOffset = -CORTAO_OCCLUDER_OFFSET_SCALE
                * occluderRadius
                / max(1.0 - occluderRadius * occluderRadius * 0.5, 0.000001);
            direction = normalize(direction + sampleNormal * adjustedOffset);
        } else {
            occluderRadius *=
                1.0 + (slopeCompensation - 1.0) * CORTAO_BACKFACE_BOOST;
        }

        // the occluder's direction in the rays' tangent frame and the cosine
        // of its angular radius, its circle wrapped onto the hemisphere
        vec3 h = vec3(dot(direction, tangent),
            dot(direction, bitangent),
            dot(direction, n));
        float occluderCosAngle =
            max(0.0, 1.0 - occluderRadius * occluderRadius * 0.5);
        for (int j = 0; j < CORTAO_RAYS; j++) {
            if (dot(rays[j], h) > occluderCosAngle)
                rayBits |= (1u << j);
        }
    }

    return 1.0 - float(bitCount(rayBits)) / float(CORTAO_RAYS);
}
