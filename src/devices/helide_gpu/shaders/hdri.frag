#version 450

// A visible hdri light where camera rays miss: drawn over the whole frame
// before the surfaces, which then cover it, adding its radiance along the
// pixel's camera ray to the color target (the frame clears it to black,
// opaque, when any hdri is visible, so the visible hdris sum). The pipeline
// writes only the color target's rgb, so the other targets keep their
// cleared "no surface" values. The lookup is halcyon's
// (src/devices/halcyon/shading/halcyon_hdri.isph): nearest texel, the map
// mapping described in light/Hdri.h.

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 FragColor;

// Fragment sampler slot 0 (set=2, binding=0): the map, RGBA32F, rows in the
// array's order (row 0 towards -up)
layout(set = 2, binding = 0) uniform sampler2D radianceTex;

// Fragment uniform buffer slot 0 (set=3, binding=0)
layout(set = 3, binding = 0) uniform HdriUniforms {
    // view space from clip space (inverse(P))
    mat4 invProj;
    // world space from view space (inverse(V); only its rotation is used)
    mat4 viewToWorld;
    // the light frame in world space (xyz): left = up x direction, up and
    // direction
    vec4 left;
    vec4 up;
    vec4 direction;
    // rgb: 'color' times 'scale'
    vec4 emission;
} u;

const float PI = 3.14159265;
// the largest float below 1
const float ONE_MINUS_EPSILON = 0.99999994;

void main()
{
    // the pixel's position in the projection's clip space: the geometry
    // shaders negate the projection's y (gl_Position.y = -gl_Position.y), so
    // it is the opposite of this triangle's, which fullscreen.vert's vUV.y
    // runs opposite to. Then its camera ray, through two points in view
    // space: the near plane and depth 0.5, both close to the camera (the far
    // plane's w underflows in float, and the camera's translation would
    // swamp near-plane offsets in world space)
    vec2 ndc = vec2(2.0 * vUV.x - 1.0, 2.0 * vUV.y - 1.0);
    vec4 p0 = u.invProj * vec4(ndc, 0.0, 1.0);
    vec4 p1 = u.invProj * vec4(ndc, 0.5, 1.0);
    vec3 dir = mat3(u.viewToWorld) * (p1.xyz / p1.w - p0.xyz / p0.w);

    // the direction in the light frame (X = left, Y = up, Z = direction)
    vec3 d = normalize(vec3(
        dot(dir, u.left.xyz), dot(dir, u.up.xyz), dot(dir, u.direction.xyz)));
    // t from 0 at +up to 1 at -up, s 0.5 along +direction and 0.25 along left
    float t = acos(clamp(d.y, -1.0, 1.0)) / PI;
    float s = fract(0.5 - atan(d.x, d.z) / (2.0 * PI));

    ivec2 size = textureSize(radianceTex, 0);
    int x = clamp(int(float(size.x) * s), 0, size.x - 1);
    int y = clamp(
        int(float(size.y) * clamp(t, 0.0, ONE_MINUS_EPSILON)), 0, size.y - 1);
    // the array's rows run from -up (row 0) to up
    vec3 radiance = texelFetch(radianceTex, ivec2(x, size.y - 1 - y), 0).rgb;

    FragColor = vec4(radiance * u.emission.rgb, 1.0);
}
