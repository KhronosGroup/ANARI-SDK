#version 450

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
layout(location = 1) out uvec4 outIds;    // R=primitiveId, G=objectId, B=instanceId
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outNormal;

// Fragment sampler at set=2, binding=0 (SDL3_gpu fragment sampler slot 0)
layout(set = 2, binding = 0) uniform sampler3D texSampler;

// Fragment uniform buffer slot 0: material + attribute data
layout(set = 3, binding = 0) uniform MaterialUniforms {
    vec4 baseColor;        // material's uniform color (RGB, alpha unused)
    vec4 uniformAttr[5];   // geometry-level fallback [color, attr0..3]
    uint attrMask;         // bitmask: which slots have per-vertex data
    int  colorAttrIndex;   // which attribute slot material samples (-1 = use baseColor)
    int  samplerMode;      // 0=uniform color, 1=geometry attribute, 2=texture sampler
    int  texCoordAttrIdx;  // which attr slot has texture coordinates (for mode 2)
    float opacity;         // surface opacity (0=transparent, 1=opaque)
    int   alphaMode;       // 0=opaque, 1=blend (screen door dither), 2=mask (binary cutoff)
    float alphaCutoff;     // opacity threshold for alphaMode=mask
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

void main()
{
    // Gather all attribute varyings into an indexable array
    vec4 attrs[5] = vec4[5](vColor, vAttr0, vAttr1, vAttr2, vAttr3);

    // Resolve the surface color based on samplerMode
    vec3 color;
    if (mat_u.samplerMode == 2) {
        // Texture sampler path
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
        // Primitive sampler: lookup per-primitive data from 1D texture
        uint primIdx = vPrimitiveIndex + samp_u.primitiveOffset;
        float u = (float(primIdx) + 0.5) / float(samp_u.arraySize);
        vec4 sampled = texture(texSampler, vec3(u, 0.5, 0.5));
        vec4 result = samp_u.outTransform * sampled + samp_u.outOffset;
        color = result.xyz;
    } else if (mat_u.colorAttrIndex >= 0) {
        // Geometry attribute path
        int idx = mat_u.colorAttrIndex;
        color = ((mat_u.attrMask & (1u << idx)) != 0u)
            ? attrs[idx].xyz
            : mat_u.uniformAttr[idx].xyz;
    } else {
        // Uniform color path
        color = mat_u.baseColor.xyz;
    }

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

    // Eye-light shading with configurable blend ratio
    vec3 N = normalize(vNormalView);
    vec3 L = normalize(-vPosView);  // vector from fragment toward camera (eye-light)
    float falloff = max(dot(N, L), 0.0);
    FragColor = vec4(color * mix(1.0, falloff, rend_u.eyeLightBlendRatio) * rend_u.ambientRadiance, 1.0);

    // Write per-pixel ID outputs (packed into one target)
    outIds = uvec4(vPrimitiveId, id_u.objectId, id_u.instanceId, 0u);

    // Write albedo (pre-lighting surface color) and world-space normal
    outAlbedo = vec4(color, 0.0);
    vec3 Nw = normalize(vNormalWorld);
    outNormal = vec4(Nw, 0.0);
}
