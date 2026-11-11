#version 450

// A visible quad light: its radiance, unlit. It writes "no surface" to the
// other targets, so the ID channels report no hit, it adds no ambient light
// (the all-ones ID .w), and ambient occlusion neither darkens it nor treats
// it as an occluder (view depth 0 in the normal target's .w).

layout(location = 0) out vec4 FragColor;
layout(location = 1) out uvec4 outIds;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outNormal;

// Fragment uniform buffer slot 0 (set=3, binding=0)
layout(set = 3, binding = 0) uniform LightQuadUniforms {
    vec4 radiance; // rgb: the seen side's radiance
} u;

void main()
{
    FragColor = vec4(u.radiance.rgb, 1.0);
    outIds = uvec4(0xFFFFFFFFu);
    outAlbedo = vec4(0.0);
    outNormal = vec4(0.0);
}
