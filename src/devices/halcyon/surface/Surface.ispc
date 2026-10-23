// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_objects.isph"
#include "shading/halcyon_util.isph"

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_Surface()
{
  return sizeof(uniform Surface);
}

export void Surface_setMaterial(void *uniform _self, void *uniform material)
{
  uniform Surface *uniform self = object_cast<Surface>(_self);
  self->material = object_cast<Material>(material);
}

export void Surface_setGeometry(void *uniform _self, void *uniform geometry)
{
  uniform Surface *uniform self = object_cast<Surface>(_self);
  self->geometry = object_cast<Geometry>(geometry);
}

export void Surface_setId(void *uniform _self, uniform uint32 id)
{
  uniform Surface *uniform self = object_cast<Surface>(_self);
  self->id = id;
}
