#version 450

// Screen-space ambient occlusion: the Alchemy/SAO estimator (McGuire et al.,
// "Scalable Ambient Obscurance", HPG 2012) in the form the renderer's
// 'ambientOcclusionEstimator' selects (G3D's high-quality code or the
// paper's), raised to the power 'ambientOcclusionIntensity'. A port of
// halcyon's preview renderer (src/devices/halcyon/renderer/preview/
// Preview.ispc, occlusionAt()); its constants and their reasoning live there.
// The blur pass (blur.frag) removes the noise of the 4x4 rotation pattern.

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 FragAO;

// Fragment samplers (set=2)
// world-space normal in .xyz, view depth in .w (0 where no surface)
layout(set = 2, binding = 0) uniform sampler2D normalTex;

// SSAO parameters (set=3, binding=0)
layout(set = 3, binding = 0) uniform SSAOParams {
    mat4 view;
    vec2 frameSize;
    // the projection's x and y scale (P[0][0], P[1][1])
    vec2 projScale;
    // world-space occlusion radius, > 0
    float radius;
    // exponent of the occlusion factor, > 0
    float intensity;
    // 0 'g3d', 1 'sao'
    int estimator;
    // 1 for a perspective camera, 0 for an orthographic one
    int perspective;
} u;

const int AO_TAPS = 16;
const float AO_SPIRAL_TURNS = 7.0;
const float AO_BIAS = 0.01;
const float AO_EPSILON = 0.01;
const float AO_G3D_SCALE = 3.0;
const float AO_SAO_SCALE = 5.0;
const float AO_TWO_PI = 6.283185307;
// Rotation of the spiral per pixel, in 16ths of a turn, keyed by the pixel's
// position modulo 4 (a 4x4 Bayer pattern); blur.frag's first pass cancels it
const int AO_ROTATIONS[16] = int[16](
    0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);

// View-space position of the surface at 'pixel' (its centre), view depth
// 'depth'
vec3 viewPosition(ivec2 pixel, float depth)
{
    vec2 uv = (vec2(pixel) + 0.5) / u.frameSize;
    // NDC: UV [0,1] -> [-1,1]; the geometry pass negates clip-space Y, which
    // puts NDC +Y at the bottom of the framebuffer (UV v = 1), so v maps to Y
    // without a flip
    vec2 ndc = uv * 2.0 - 1.0;
    vec2 xy = ndc / u.projScale;
    return vec3(u.perspective != 0 ? xy * depth : xy, -depth);
}

void main()
{
    ivec2 pixel = ivec2(vUV * u.frameSize);
    vec4 normalDepth = texelFetch(normalTex, pixel, 0);

    // No surface (background, or covered by a volume): unoccluded
    if (!(normalDepth.w > 0.0)) {
        FragAO = vec4(1.0);
        return;
    }

    vec3 P = viewPosition(pixel, normalDepth.w);
    vec3 n = normalize(mat3(u.view) * normalDepth.xyz);
    // On the viewer's side of the surface, as the occlusion hemisphere is:
    // the direction to the viewer is -P for a perspective camera, +z for an
    // orthographic one
    vec3 toViewer = u.perspective != 0 ? -P : vec3(0.0, 0.0, 1.0);
    if (dot(n, toViewer) < 0.0)
        n = -n;

    // The occlusion radius in pixels at the surface's depth (the same at every
    // depth for an orthographic camera)
    vec2 radiusPixels = 0.5 * u.frameSize * u.projScale * u.radius;
    if (u.perspective != 0)
        radiusPixels /= normalDepth.w;

    float rotation =
        float(AO_ROTATIONS[(pixel.y & 3) * 4 + (pixel.x & 3)]) * (AO_TWO_PI / 16.0);
    vec2 center = vec2(pixel) + 0.5;
    float invRadius = 1.0 / u.radius;
    float invRadius2 = invRadius * invRadius;
    float bias = AO_BIAS * u.radius;
    float epsilon = AO_EPSILON * u.radius * u.radius;

    float sum = 0.0;
    int landed = 0;
    for (int k = 0; k < AO_TAPS; ++k) {
        float alpha = (float(k) + 0.5) / float(AO_TAPS);
        float angle = alpha * (AO_SPIRAL_TURNS * AO_TWO_PI) + rotation;
        vec2 p = center + alpha * radiusPixels * vec2(cos(angle), sin(angle));
        // Taps outside the frame are dropped (NaN too)
        if (!(p.x >= 0.0 && p.x < u.frameSize.x && p.y >= 0.0
                && p.y < u.frameSize.y))
            continue;
        ivec2 tap = ivec2(p);
        float tapDepth = texelFetch(normalTex, tap, 0).w;
        // So are taps with no surface
        if (!(tapDepth > 0.0))
            continue;
        landed++;

        vec3 v = viewPosition(tap, tapDepth) - P;
        float vv = dot(v, v);
        if (u.estimator == 1) {
            // The paper's tap (r^2 - v.v)^3 max(v.n - b, 0) / (v.v + epsilon),
            // 0 outside the radius, over r^5
            float vvRelative = vv * invRadius2;
            float falloff = max(1.0 - vvRelative, 0.0);
            sum += falloff * falloff * falloff
                * max((dot(v, n) - bias) * invRadius, 0.0)
                / (vvRelative + AO_EPSILON);
        } else {
            // G3D's falloff, cut to 0 at the radius
            float falloff = max(1.0 - vv * invRadius2, 0.0);
            sum += falloff * max((dot(v, n) - bias) * inversesqrt(vv + epsilon), 0.0);
        }
    }

    float ao = 1.0;
    if (landed > 0) {
        float factor = u.estimator == 1
            ? max(1.0 - sum * (AO_SAO_SCALE / float(landed)), 0.0)
            : max(1.0 - sqrt(sum * (AO_G3D_SCALE / float(landed))), 0.0);
        ao = pow(factor, u.intensity);
    }

    FragAO = vec4(ao, ao, ao, 1.0);
}
