#version 450

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 FragColor;

// Fragment samplers (set=2)
layout(set = 2, binding = 0) uniform sampler2D colorTex;
layout(set = 2, binding = 1) uniform sampler2D aoTex;

// Fragment uniform buffer slot 0 (set=3, binding=0)
layout(set = 3, binding = 0) uniform CompositeUniforms {
    int applyAO;
    int applySRGB;
} comp_u;

void main()
{
    vec4 color = texture(colorTex, vUV);
    if (comp_u.applyAO != 0) {
        float ao = texture(aoTex, vUV).r;
        color.rgb *= ao;
    }
    if (comp_u.applySRGB != 0)
        color.rgb = pow(color.rgb, vec3(1.0 / 2.2));
    FragColor = color;
}
