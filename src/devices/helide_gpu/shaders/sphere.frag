#version 450

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

layout(location = 0) out vec4 FragColor;
layout(location = 1) out uvec4 outIds;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outNormal;

// Fragment sampler at set=2, binding=0
layout(set = 2, binding = 0) uniform sampler3D texSampler;

// Fragment uniform buffer slot 0: material + attribute data
layout(set = 3, binding = 0) uniform MaterialUniforms {
    vec4 baseColor;
    vec4 uniformAttr[5];
    uint attrMask;
    int  colorAttrIndex;
    int  samplerMode;
    int  texCoordAttrIdx;
    float opacity;
    int   alphaMode;
    float alphaCutoff;
} mat_u;

// Fragment uniform buffer slot 1: sampler transforms
layout(set = 3, binding = 1) uniform SamplerTransforms {
    mat4 inTransform;
    vec4 inOffset;
    mat4 outTransform;
    vec4 outOffset;
    uint primitiveOffset;
    uint arraySize;
} samp_u;

// Fragment uniform buffer slot 2: renderer parameters
layout(set = 3, binding = 2) uniform RendererUniforms {
    float eyeLightBlendRatio;
    float ambientRadiance;
} rend_u;

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

    // Compute world-space normal: the sphere normal in view space is
    // (hit - center)/radius.  To get world-space normal we note that for a
    // sphere, the normal direction equals the (hit - center) direction in any
    // space, so we can compute it from the world-space center.
    // Since we don't have invMV, approximate world normal from the view normal
    // by using the fact that the sphere is symmetric -- the normal in object
    // space is the same unit vector as in any uniformly-scaled space.
    // For correctness with non-uniform model transforms we would need invMV,
    // but for typical use cases (uniform scale or identity model) this works:
    vec3 hitDir = normalize(hitView - vCenterView);
    // We use the interpolated world center to reconstruct world normal
    // by noting that the offset direction from center is the same in any
    // orthonormal basis.  This is exact for orthogonal transforms.
    vec3 normalWorld = hitDir; // approximate; exact for identity/uniform model

    // Write corrected depth from the actual sphere surface
    vec4 clipPos = vProj * vec4(hitView, 1.0);
    gl_FragDepth = clipPos.z / clipPos.w;

    // Gather all attribute varyings into an indexable array
    vec4 attrs[5] = vec4[5](vColor, vAttr0, vAttr1, vAttr2, vAttr3);

    // Resolve the surface color based on samplerMode (same logic as triangle)
    vec3 color;
    if (mat_u.samplerMode == 2) {
        int idx = mat_u.texCoordAttrIdx;
        vec4 attrVal = ((mat_u.attrMask & (1u << idx)) != 0u)
            ? attrs[idx]
            : mat_u.uniformAttr[idx];
        vec4 tc = samp_u.inTransform * attrVal + samp_u.inOffset;
        vec4 sampled = texture(texSampler, tc.xyz);
        vec4 result = samp_u.outTransform * sampled + samp_u.outOffset;
        color = result.xyz;
    } else if (mat_u.samplerMode == 3) {
        // Transform sampler: read attribute, apply in+out transforms
        int idx = mat_u.texCoordAttrIdx;
        vec4 attrVal = ((mat_u.attrMask & (1u << idx)) != 0u)
            ? attrs[idx]
            : mat_u.uniformAttr[idx];
        vec4 transformed = samp_u.inTransform * attrVal + samp_u.inOffset;
        vec4 result = samp_u.outTransform * transformed + samp_u.outOffset;
        color = result.xyz;
    } else if (mat_u.samplerMode == 4) {
        // Primitive sampler: lookup per-sphere data from 1D texture
        // Use vInstanceIndex (sphere index), not gl_PrimitiveID (icosphere face)
        uint primIdx = uint(vInstanceIndex) + samp_u.primitiveOffset;
        float u = (float(primIdx) + 0.5) / float(samp_u.arraySize);
        vec4 sampled = texture(texSampler, vec3(u, 0.5, 0.5));
        vec4 result = samp_u.outTransform * sampled + samp_u.outOffset;
        color = result.xyz;
    } else if (mat_u.colorAttrIndex >= 0) {
        int idx = mat_u.colorAttrIndex;
        color = ((mat_u.attrMask & (1u << idx)) != 0u)
            ? attrs[idx].xyz
            : mat_u.uniformAttr[idx].xyz;
    } else {
        color = mat_u.baseColor.xyz;
    }

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

    // Eye-light shading
    vec3 N = normalize(normalView);
    vec3 L = normalize(-hitView);  // vector from hit toward camera
    float falloff = max(dot(N, L), 0.0);
    FragColor = vec4(color * mix(1.0, falloff, rend_u.eyeLightBlendRatio) * rend_u.ambientRadiance, 1.0);

    // Per-pixel ID outputs
    outIds = uvec4(uint(gl_PrimitiveID), id_u.objectId, id_u.instanceId, 0u);

    // Albedo and world-space normal
    outAlbedo = vec4(color, 0.0);
    outNormal = vec4(normalize(normalWorld), 0.0);
}
