#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) flat in vec3 vCenterView;
layout(location = 1) flat in float vRadius;
layout(location = 2) in vec3 vPosView;
layout(location = 3) flat in vec4 vColor;
layout(location = 4) flat in vec4 vAttr0;
layout(location = 5) flat in vec4 vAttr1;
layout(location = 6) flat in vec4 vAttr2;
layout(location = 7) flat in vec4 vAttr3;
layout(location = 8) flat in vec3 vCenterWorld;
layout(location = 9) flat in mat4 vProj;
layout(location = 13) flat in int vInstanceIndex;
layout(location = 14) in vec3 vPosWorld;
layout(location = 15) flat in vec3 vEyeWorld;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out uvec4 outIds;    // A: packed ambient light
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

// Ray-sphere intersection in view space.
// Returns the nearest positive t along the ray, or -1.0 if no hit.
// ray: origin + t * dir
float intersectSphere(vec3 origin, vec3 dir, vec3 center, float radius)
{
    vec3 oc = origin - center;
    float a = dot(dir, dir);
    float b = 2.0 * dot(oc, dir);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
        return -1.0;
    float t = (-b - sqrt(discriminant)) / (2.0 * a);
    return t;
}

void main()
{
    // Ray from the eye (origin in view space) through this fragment
    vec3 rayDir = normalize(vPosView);
    float t = intersectSphere(vec3(0.0), rayDir, vCenterView, vRadius);
    if (t < 0.0)
        discard;

    // Compute hit point and normal in view space
    vec3 hitView = rayDir * t;
    vec3 normalView = normalize(hitView - vCenterView);

    // World-space normal: the same hit along the world-space ray, as the view
    // transform is rigid and so keeps the distance t
    vec3 hitWorld = vEyeWorld + normalize(vPosWorld - vEyeWorld) * t;
    vec3 normalWorld = hitWorld - vCenterWorld;

    // Write corrected depth from the actual sphere surface
    vec4 clipPos = vProj * vec4(hitView, 1.0);
    gl_FragDepth = clipPos.z / clipPos.w;

    // Gather all attribute varyings into an indexable array
    vec4 attrs[5] = vec4[5](vColor, vAttr0, vAttr1, vAttr2, vAttr3);

    // Primitive samplers index by the sphere, not gl_PrimitiveID (an icosphere face)
    uint primIdx = uint(vInstanceIndex);
    vec3 color = materialColor(attrs, primIdx);

    // Screen door transparency
    if (mat_u.alphaMode == 1) {
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
        if (mat_u.opacity < mat_u.alphaCutoff)
            discard;
    }

    // Storm's UsdPreviewSurface shading (the ray hits the side facing the
    // viewer)
    vec3 P = hitView;
    vec3 N = normalView;
    float metallic = materialMetallic(attrs, primIdx);
    float roughness = materialRoughness(attrs, primIdx);
    ShadingResult shading =
        shade(P, N, color, metallic, roughness, mat_u.ior);
    vec3 direct = shading.direct + mat_u.emissive.rgb;
    FragColor = vec4(
        light_u.deferAmbient != 0 ? direct : direct + shading.ambient, 1.0);

    // Per-pixel ID outputs
    outIds = uvec4(uint(gl_PrimitiveID), id_u.objectId, id_u.instanceId,
        packAmbient(shading.ambient));

    // Albedo and world-space normal
    outAlbedo = vec4(color, 0.0);
    // .w: the view depth, from which the SSAO and blur passes rebuild the
    // view-space position
    outNormal = vec4(normalize(normalWorld), -hitView.z);
}
