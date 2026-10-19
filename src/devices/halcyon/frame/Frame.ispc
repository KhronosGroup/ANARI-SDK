// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_objects.isph"
#include "shading/halcyon_util.isph"

// Exported interface /////////////////////////////////////////////////////////

export uniform int get_programCount()
{
  return programCount;
}

export uniform ulong sizeof_Frame()
{
  return sizeof(uniform Frame);
}

export void Frame_setBuffers(
    void *uniform _self, void *uniform color, void *uniform depth)
{
  uniform Frame *uniform self = object_cast<Frame>(_self);
  self->color = color;
  self->depth = (uniform float *uniform)depth;
}

export void Frame_setIdBuffers(void *uniform _self,
    void *uniform primId,
    void *uniform objId,
    void *uniform instId)
{
  uniform Frame *uniform self = object_cast<Frame>(_self);
  self->primId = (uniform uint32 * uniform) primId;
  self->objId = (uniform uint32 * uniform) objId;
  self->instId = (uniform uint32 * uniform) instId;
}

export void Frame_setAlbedoAndNormalBuffers(void *uniform _self,
    uniform int albedoFormat,
    void *uniform albedo,
    uniform int normalFormat,
    void *uniform normal)
{
  uniform Frame *uniform self = object_cast<Frame>(_self);
  self->albedoFormat = albedoFormat;
  self->albedo = albedo;
  self->normalFormat = normalFormat;
  self->normal = normal;
}

export void Frame_setColorFormat(void *uniform _self, uniform int format)
{
  uniform Frame *uniform self = object_cast<Frame>(_self);
  self->colorFormat = format;
}

export void Frame_setSize(
    void *uniform _self, uniform int size_x, uniform int size_y)
{
  uniform Frame *uniform self = object_cast<Frame>(_self);
  self->size.x = size_x;
  self->size.y = size_y;
  self->invSize =
      (size_x > 0 && size_y > 0) ? 1.f / self->size : make_float2(0.f, 0.f);
  self->aspect = size_y > 0 ? (float)size_x / size_y : 0.f;
}

export void Frame_setInvalidMaterialColor(
    void *uniform _self, const uniform float *uniform color /* float4 */)
{
  uniform Frame *uniform self = object_cast<Frame>(_self);
  self->invalidMaterialColor = load_float4(color);
}

export void Frame_setRenderer(void *uniform _self, void *uniform renderer)
{
  uniform Frame *uniform self = object_cast<Frame>(_self);
  self->renderer = object_cast<Renderer>(renderer);
}

export void Frame_setCamera(void *uniform _self, void *uniform camera)
{
  uniform Frame *uniform self = object_cast<Frame>(_self);
  self->camera = object_cast<Camera>(camera);
}

export void Frame_setWorld(void *uniform _self, void *uniform world)
{
  uniform Frame *uniform self = object_cast<Frame>(_self);
  self->world = object_cast<World>(world);
}
