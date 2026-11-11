#version 450

// Depth-aware blur of the occlusion factor, a port of halcyon's preview
// renderer (src/devices/halcyon/renderer/preview/Preview.ispc,
// blurredOcclusionAt()): the weighted mean of the factors in the 5x5 window
// centred on the pixel, whose offsets are multiplied by the pass's stride (1
// for the first pass, 2^(pass - 1) after it, an a-trous pass), over the
// pixels with a surface whose distance from the camera is within the
// occlusion radius of the pixel's. A neighbour's weight is its spatial weight
// times a depth weight that falls from 1 to 0 at the radius as
// 1 - (difference / radius)^2, so occlusion doesn't bleed across silhouettes.

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 FragBlurred;

// Fragment samplers (set=2)
layout(set = 2, binding = 0) uniform sampler2D aoTex;
// world-space normal in .xyz, view depth in .w (0 where no surface)
layout(set = 2, binding = 1) uniform sampler2D normalTex;

// Blur parameters (set=3, binding=0)
layout(set = 3, binding = 0) uniform BlurParams {
    vec2 frameSize;
    // the projection's x and y scale (P[0][0], P[1][1])
    vec2 projScale;
    // world-space occlusion radius, > 0
    float radius;
    // 1 for the first pass
    int pass;
    // 1 for a perspective camera, 0 for an orthographic one
    int perspective;
} u;

const int AO_BLUR_RADIUS = 2;
// First pass: the window is one wider than ssao.frag's 4x4 rotation pattern,
// so its outer offsets fall on the same column (row) of the pattern; they
// weigh half each, so every rotation gets total weight 1 and the pattern
// cancels exactly
const float AO_BLUR_WEIGHTS[5] = float[5](0.5, 1.0, 1.0, 1.0, 0.5);
// Later passes: the B3-spline of the a-trous transform
const float AO_ATROUS_WEIGHTS[5] =
    float[5](1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0);

// Distance from the camera of the surface at 'pixel' (its centre), view
// depth 'depth': along the pixel's ray for a perspective camera, the view
// depth for an orthographic one
float cameraDistance(ivec2 pixel, float depth)
{
    if (u.perspective == 0)
        return depth;
    vec2 uv = (vec2(pixel) + 0.5) / u.frameSize;
    vec2 ndc = uv * 2.0 - 1.0;
    return depth * length(vec3(ndc / u.projScale, 1.0));
}

void main()
{
    ivec2 pixel = ivec2(vUV * u.frameSize);
    ivec2 size = ivec2(u.frameSize);
    float depth = texelFetch(normalTex, pixel, 0).w;
    if (!(depth > 0.0)) {
        FragBlurred = vec4(1.0);
        return;
    }

    float distance = cameraDistance(pixel, depth);
    float invRadius = 1.0 / u.radius;
    int stride = 1 << (u.pass - 1);

    float sum = 0.0;
    float weightSum = 0.0;
    for (int dy = -AO_BLUR_RADIUS; dy <= AO_BLUR_RADIUS; ++dy) {
        int ny = pixel.y + dy * stride;
        if (ny < 0 || ny >= size.y)
            continue;
        float weightY = u.pass == 1 ? AO_BLUR_WEIGHTS[dy + AO_BLUR_RADIUS]
                                    : AO_ATROUS_WEIGHTS[dy + AO_BLUR_RADIUS];
        for (int dx = -AO_BLUR_RADIUS; dx <= AO_BLUR_RADIUS; ++dx) {
            int nx = pixel.x + dx * stride;
            if (nx < 0 || nx >= size.x)
                continue;
            ivec2 neighbour = ivec2(nx, ny);
            float d = texelFetch(normalTex, neighbour, 0).w;
            if (!(d > 0.0))
                continue;
            float difference =
                abs(cameraDistance(neighbour, d) - distance) * invRadius;
            if (!(difference < 1.0))
                continue;
            float weightX = u.pass == 1 ? AO_BLUR_WEIGHTS[dx + AO_BLUR_RADIUS]
                                        : AO_ATROUS_WEIGHTS[dx + AO_BLUR_RADIUS];
            float weight = weightY * weightX * (1.0 - difference * difference);
            sum += weight * texelFetch(aoTex, neighbour, 0).r;
            weightSum += weight;
        }
    }

    // The pixel itself has a positive weight
    float ao = sum / weightSum;
    FragBlurred = vec4(ao, ao, ao, 1.0);
}
