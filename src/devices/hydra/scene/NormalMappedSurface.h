// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace anari_hydra {

// The generated normal-mapping surface (SPEC §6.5).
//
// Storm's UsdPreviewSurface perturbs the normal only when `normal` comes from a
// texture: `perturbFragmentNormal` is compiled under HD_HAS_COORD_normal and
// builds the tangent frame from that texture's st. A transform/primitive
// sampler's value is a baked primvar, not a texture, so a material with one on
// `normal` gets a second, Storm-only network on this variant instead. It
// imports Storm's own previewSurface.glslfx and swaps only the normal-mapping
// section: the `normal` input arrives straight from the primvar — already the
// [-1,1] tangent-space normal, since ANARI leaves the [0,1]->[-1,1] decode to
// the app's outTransform/outOffset, which the bake applied — and is applied in
// the tangent frame Storm builds from the geometry's `st` (attribute0), read
// through the extra `anariTangentSt` input. Without a varying `st` the frame
// degenerates and the normal is left unperturbed, as for an image normal map.
// The source is a constant, so Sdr registers exactly one node for the process
// and Storm's program cache sees one shader per primvar layout.
//
// It is tied to OpenUSD internals: the parameter list copies Sdr's
// UsdPreviewSurface node and the technique names previewSurface.glslfx's
// section names. tests/guard_normal_map_shader checks both against the OpenUSD
// in use (README.md, "OpenUSD upgrade checklist").

// The extra input the generated node reads `st` through, and the section that
// replaces previewSurface.glslfx's Preview.NormalMapping.
inline constexpr char kNormalMappedSurfaceTangentStInput[] = "anariTangentSt";
inline constexpr char kNormalMappedSurfaceSection[] =
    "AnariPrimvarNormalMap.NormalMapping";

inline constexpr char kNormalMappedSurfaceSource[] =
    R"GLSLFX(-- glslfx version 0.1
#import $TOOLS/usdShaders/shaders/previewSurface.glslfx

-- configuration
{
    "parameters": {
        "diffuseColor": { "default": [0.18, 0.18, 0.18] },
        "emissiveColor": { "default": [0.0, 0.0, 0.0] },
        "useSpecularWorkflow": { "default": 0 },
        "specularColor": { "default": [0.0, 0.0, 0.0] },
        "metallic": { "default": 0.0 },
        "roughness": { "default": 0.5 },
        "clearcoat": { "default": 0.0 },
        "clearcoatRoughness": { "default": 0.01 },
        "opacity": { "default": 1.0 },
        "opacityMode": { "default": 1 },
        "opacityThreshold": { "default": 0.0 },
        "ior": { "default": 1.5 },
        "normal": { "default": [0.0, 0.0, 1.0] },
        "displacement": { "default": 0.0 },
        "occlusion": { "default": 1.0 },
        "anariTangentSt": { "default": [0.0, 0.0] }
    },
    "techniques": {
        "default": {
            "displacementShader": {
                "source": [ "Preview.Displacement" ]
            },
            "surfaceShader": {
                "source": [ "Preview.LightStructures",
                            "SurfaceHelpers.Lighting",
                            "Preview.Lighting",
                            "Preview.LightIntegration",
                            "SurfaceHelpers.TangentSpace",
                            "AnariPrimvarNormalMap.NormalMapping",
                            "Preview.Surface" ]
            }
        }
    }
}

-- glsl AnariPrimvarNormalMap.NormalMapping

vec3
perturbFragmentNormal(vec3 P, vec3 N)
{
    const vec3 v = HdGet_normal().xyz;
    const mat3 TBN = ComputeTBNMatrix(P, N, HdGet_anariTangentSt().xy);
    // No st variation (absent or constant st): no tangent frame, no map.
    if (dot(v, v) < 1e-12 || dot(TBN[0], TBN[0]) < 1e-12)
        return N;
    const vec3 n = TBN * normalize(v);
    return dot(n, n) > 1e-12 ? normalize(n) : N;
}
)GLSLFX";

} // namespace anari_hydra
