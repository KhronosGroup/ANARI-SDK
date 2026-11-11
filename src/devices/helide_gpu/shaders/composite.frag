#version 450
#extension GL_GOOGLE_include_directive : require

#include "ambient_packing.glsl"

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 FragColor;

// Fragment samplers (set=2)
layout(set = 2, binding = 0) uniform sampler2D colorTex;
layout(set = 2, binding = 1) uniform sampler2D aoTex;
// .w: the surfaces' packed ambient light, left out of colorTex when applyAO
// is set
layout(set = 2, binding = 2) uniform usampler2D idsTex;
// .w: the opacity of a volume in front of the surface
layout(set = 2, binding = 3) uniform sampler2D albedoTex;

// Fragment uniform buffer slot 0 (set=3, binding=0)
layout(set = 3, binding = 0) uniform CompositeUniforms {
    int applyAO;
    int applySRGB;
} comp_u;

// Exact sRGB encoding of linear 'c' (as Storm's color correction)
vec3 linearToSRGB(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    return mix(12.92 * c,
        1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055,
        greaterThan(c, vec3(0.0031308)));
}

void main()
{
    vec4 color = texture(colorTex, vUV);
    // Ambient occlusion darkens only the ambient light, which reaches the
    // viewer through any volume in front
    if (comp_u.applyAO != 0) {
        ivec2 pixel = ivec2(gl_FragCoord.xy);
        vec3 ambient = unpackAmbient(texelFetch(idsTex, pixel, 0).w);
        float transmittance = 1.0 - texelFetch(albedoTex, pixel, 0).w;
        color.rgb += texture(aoTex, vUV).r * transmittance * ambient;
    }
    if (comp_u.applySRGB != 0)
        color.rgb = linearToSRGB(color.rgb);
    FragColor = color;
}
