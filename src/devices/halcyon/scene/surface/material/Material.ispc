// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_objects.isph"
#include "shading/halcyon_util.isph"

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_Material()
{
  return sizeof(uniform Material);
}

export void Material_setParameters(void *uniform _self,
    const uniform float *uniform color, /* float4 */
    uniform Attribute colorAttribute,
    uniform float opacity,
    uniform Attribute opacityAttribute,
    uniform AlphaMode alphaMode,
    uniform float alphaCutoff,
    uniform bool colorAlphaIsOpacity)
{
  uniform Material *uniform self = object_cast<Material>(_self);
  self->color = load_float4(color);
  self->colorAttribute = colorAttribute;
  self->opacity = opacity;
  self->opacityAttribute = opacityAttribute;
  self->alphaMode = alphaMode;
  self->alphaCutoff = alphaCutoff;
  self->colorAlphaIsOpacity = colorAlphaIsOpacity;
}

// How the pathtracer shades the material; 'cppMaterial' is the C++ material
// for MATERIAL_KIND_CPP and native kinds, NULL otherwise, 'attributeMask'
// the attributes it reads (bit (1 << a) for Attribute a), 'contextMask'
// the parts of the shading context it reads (PathContextGroup bits) and
// 'readsObjectPosition' whether those include PATH_CONTEXT_OBJECT_POSITION
// (MATERIAL_READS_OBJECT_POSITION)
export void Material_setKind(void *uniform _self,
    uniform MaterialKind kind,
    const void *uniform cppMaterial,
    uniform uint32 attributeMask,
    uniform uint32 contextMask,
    uniform bool readsObjectPosition)
{
  uniform Material *uniform self = object_cast<Material>(_self);
  self->kind = kind;
  self->cppMaterial = cppMaterial;
  self->attributeMask = attributeMask
      | (readsObjectPosition ? MATERIAL_READS_OBJECT_POSITION : 0u);
  self->contextMask = contextMask;
}

// 'color'/'baseColor' and 'opacity' samplers (their ISPC objects), NULL if not
// set
export void Material_setSamplers(void *uniform _self,
    void *uniform colorSampler,
    void *uniform opacitySampler)
{
  uniform Material *uniform self = object_cast<Material>(_self);
  self->colorSampler = object_cast<Sampler>(colorSampler);
  self->opacitySampler = object_cast<Sampler>(opacitySampler);
}

// MATERIAL_KIND_PHYSICALLY_BASED's parameters (held by the C++ material),
// NULL for every other kind
export void Material_setPhysicallyBased(void *uniform _self,
    const uniform PhysicallyBasedParameters *uniform parameters)
{
  uniform Material *uniform self = object_cast<Material>(_self);
  self->physicallyBased = parameters;
}
