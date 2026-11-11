#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vNormalView;
layout(location = 1) in vec3 vPosView;
layout(location = 2) in vec4 vColor;
layout(location = 3) in vec4 vAttr0;
layout(location = 4) in vec4 vAttr1;
layout(location = 5) in vec4 vAttr2;
layout(location = 6) in vec4 vAttr3;
layout(location = 7) in vec3 vNormalWorld;
layout(location = 8) flat in uint vPrimitiveIndex;
layout(location = 9) flat in uint vPrimitiveId;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out uvec4 outIds;    // R=primitiveId, G=objectId, B=instanceId, A=packed ambient
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outNormal;

// The material uniforms and samplers (fragment sampler slots 0..2, uniform
// buffer slots 0 and 1)
#include "material.glsl"

// Fragment uniform buffer slot 2: lighting (LightingUniforms)
#include "shading.glsl"

// Fragment uniform buffer slot 3: per-draw-call ID data
layout(set = 3, binding = 3) uniform IDUniforms {
    uint objectId;
    uint instanceId;
} id_u;

void main()
{
    // The geometric normal, from the derivatives of the position (before any
    // discard, which leaves derivatives undefined)
    vec3 Ng = cross(dFdx(vPosView), dFdy(vPosView));

    // Gather all attribute varyings into an indexable array
    vec4 attrs[5] = vec4[5](vColor, vAttr0, vAttr1, vAttr2, vAttr3);

    // Primitive samplers index by the triangle
    uint primIdx = vPrimitiveIndex;
    vec3 color = materialColor(attrs, primIdx);

    // Screen door transparency
    if (mat_u.alphaMode == 1) {
        // 4x4 Bayer dither pattern
        const float bayer[16] = float[16](
             0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
            12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
             3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
            15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
        );
        ivec2 fc = ivec2(gl_FragCoord.xy) % 4;
        if (mat_u.opacity <= bayer[fc.y * 4 + fc.x])
            discard;
    } else if (mat_u.alphaMode == 2) {
        // Binary alpha mask cutoff
        if (mat_u.opacity < mat_u.alphaCutoff)
            discard;
    }

    // Storm's UsdPreviewSurface shading, the normal turned to the viewer
    vec3 P = vPosView;
    vec3 N = faceForward(normalize(vNormalView), Ng, P);
    float metallic = materialMetallic(attrs, primIdx);
    float roughness = materialRoughness(attrs, primIdx);
    ShadingResult shading =
        shade(P, N, color, metallic, roughness, mat_u.ior);
    vec3 direct = shading.direct + mat_u.emissive.rgb;
    FragColor = vec4(
        light_u.deferAmbient != 0 ? direct : direct + shading.ambient, 1.0);

    // Write per-pixel ID outputs (packed into one target)
    outIds = uvec4(vPrimitiveId, id_u.objectId, id_u.instanceId,
        packAmbient(shading.ambient));

    // Write albedo (pre-lighting surface color) and world-space normal
    outAlbedo = vec4(color, 0.0);
    vec3 Nw = normalize(vNormalWorld);
    // .w: the view depth, from which the SSAO and blur passes rebuild the
    // view-space position
    outNormal = vec4(Nw, -vPosView.z);
}
