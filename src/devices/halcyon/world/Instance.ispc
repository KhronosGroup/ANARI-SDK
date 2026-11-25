// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_objects.isph"
#include "shading/halcyon_util.isph"

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_Instance()
{
  return sizeof(uniform Instance);
}

export void Instance_setGroup(void *uniform _self, void *uniform group)
{
  uniform Instance *uniform self = object_cast<Instance>(_self);
  self->group = object_cast<Group>(group);
}

export void Instance_setNormalXfms(
    void *uniform _self, const uniform float *uniform normalXfms)
{
  uniform Instance *uniform self = object_cast<Instance>(_self);
  self->normalXfms = normalXfms;
}

export void Instance_setIds(
    void *uniform _self, const uniform uint32 *uniform ids)
{
  uniform Instance *uniform self = object_cast<Instance>(_self);
  self->ids = ids;
}

// Transforms and their inverses, packed column-major 4x4, one per transform
export void Instance_setXfms(void *uniform _self,
    const uniform float *uniform xfms,
    const uniform float *uniform invXfms)
{
  uniform Instance *uniform self = object_cast<Instance>(_self);
  self->xfms = xfms;
  self->invXfms = invXfms;
}

// Attribute 'attr' (< NUM_GEOMETRY_ATTRIBUTES) as packed float4, one per
// transform, or NULL if unset
export void Instance_setAttribute(
    void *uniform _self, uniform int attr, const uniform float *uniform values)
{
  uniform Instance *uniform self = object_cast<Instance>(_self);
  self->attributes[attr] = values;
}

export void Instance_setNumTransforms(
    void *uniform _self, uniform uint32 numTransforms)
{
  uniform Instance *uniform self = object_cast<Instance>(_self);
  self->numTransforms = numTransforms;
}
