// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The material inputs of the surface fragment shaders (triangle.frag,
// sphere.frag): the material uniforms, and the samplers that may drive the
// base color, metallic and roughness. Surface::pushFragmentUniforms() fills
// both uniform blocks; their C++ mirrors there must match the std140 layout.

// Fragment samplers at set=2, binding=N (SDL3_gpu fragment sampler slot N),
// one per material parameter that can take a sampler
#define SAMPLER_COLOR 0
#define SAMPLER_METALLIC 1
#define SAMPLER_ROUGHNESS 2
#define NUM_MATERIAL_SAMPLERS 3

layout(set = 2, binding = 0) uniform sampler3D colorTex;
layout(set = 2, binding = 1) uniform sampler3D metallicTex;
layout(set = 2, binding = 2) uniform sampler3D roughnessTex;

// Fragment uniform buffer slot 0: material + attribute data
layout(set = 3, binding = 0) uniform MaterialUniforms {
    vec4 baseColor;        // material's uniform color (RGB, alpha unused)
    vec4 uniformAttr[5];   // geometry-level fallback [color, attr0..3]
    uint attrMask;         // bitmask: which slots have per-vertex data
    int  colorAttrIndex;   // attribute slot for the color (-1 = baseColor)
    float opacity;         // surface opacity (0=transparent, 1=opaque)
    int   alphaMode;       // 0=opaque, 1=blend (screen door dither), 2=mask (binary cutoff)
    float alphaCutoff;     // opacity threshold for alphaMode=mask
    float metallic;
    float roughness;
    float ior;
    int   metallicAttrIdx;   // attribute slot for metallic (-1 = uniform)
    int   roughnessAttrIdx;  // attribute slot for roughness (-1 = uniform)
    int   _pad0;
    int   _pad1;
    vec4  emissive;        // rgb: emitted radiance
} mat_u;

// One sampler's transforms; mode 0 means the parameter has no sampler
struct SamplerParams {
    mat4 inTransform;
    vec4 inOffset;
    mat4 outTransform;
    vec4 outOffset;
    int  mode;             // 0=none, 2=image, 3=transform, 4=primitive
    int  inAttrIdx;        // attribute slot of the sampler's input
    uint primitiveOffset;  // primitive sampler only
    uint arraySize;        // primitive sampler only
};

// Fragment uniform buffer slot 1: the samplers, indexed by SAMPLER_*
layout(set = 3, binding = 1) uniform SamplerUniforms {
    SamplerParams p[NUM_MATERIAL_SAMPLERS];
} samp_u;

// An attribute slot's value: per-vertex/per-primitive data when the geometry
// has it, its uniform fallback otherwise
vec4 readAttribute(int idx, vec4 attrs[5])
{
    return ((mat_u.attrMask & (1u << idx)) != 0u) ? attrs[idx]
                                                   : mat_u.uniformAttr[idx];
}

// A sampler's output. primIdx is the primitive a primitive sampler looks up.
vec4 evalSampler(sampler3D tex, SamplerParams p, vec4 attrs[5], uint primIdx)
{
    vec4 v;
    if (p.mode == 4) {
        // Primitive sampler: per-primitive data in a 1D texture
        uint i = primIdx + p.primitiveOffset;
        float u = (float(i) + 0.5) / float(p.arraySize);
        v = texture(tex, vec3(u, 0.5, 0.5));
    } else {
        vec4 tc = p.inTransform * readAttribute(p.inAttrIdx, attrs) + p.inOffset;
        // Image sampler: look up the texture; transform sampler: pass through
        v = p.mode == 2 ? texture(tex, tc.xyz) : tc;
    }
    return p.outTransform * v + p.outOffset;
}

vec3 materialColor(vec4 attrs[5], uint primIdx)
{
    SamplerParams p = samp_u.p[SAMPLER_COLOR];
    if (p.mode != 0)
        return evalSampler(colorTex, p, attrs, primIdx).xyz;
    if (mat_u.colorAttrIndex >= 0)
        return readAttribute(mat_u.colorAttrIndex, attrs).xyz;
    return mat_u.baseColor.xyz;
}

// A sampler drives a scalar parameter through its first output component
float materialMetallic(vec4 attrs[5], uint primIdx)
{
    SamplerParams p = samp_u.p[SAMPLER_METALLIC];
    if (p.mode != 0)
        return clamp(evalSampler(metallicTex, p, attrs, primIdx).x, 0.0, 1.0);
    if (mat_u.metallicAttrIdx >= 0)
        return clamp(readAttribute(mat_u.metallicAttrIdx, attrs).x, 0.0, 1.0);
    return mat_u.metallic;
}

float materialRoughness(vec4 attrs[5], uint primIdx)
{
    SamplerParams p = samp_u.p[SAMPLER_ROUGHNESS];
    if (p.mode != 0)
        return clamp(evalSampler(roughnessTex, p, attrs, primIdx).x, 0.0, 1.0);
    if (mat_u.roughnessAttrIdx >= 0)
        return clamp(readAttribute(mat_u.roughnessAttrIdx, attrs).x, 0.0, 1.0);
    return mat_u.roughness;
}
