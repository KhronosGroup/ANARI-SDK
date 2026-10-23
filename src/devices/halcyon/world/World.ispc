// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_objects.isph"
#include "shading/halcyon_util.isph"

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_World()
{
  return sizeof(uniform World);
}

export void World_setEmbreeScene(void *uniform _self, void *uniform scene)
{
  uniform World *uniform self = object_cast<World>(_self);
  self->scene = (RTCScene)scene;
}

export void World_setInstances(void *uniform _self, void *uniform instances)
{
  uniform World *uniform self = object_cast<World>(_self);
  self->instances = object_array_cast<Instance>(instances);
}

// The instances whose group has volumes
export void World_setVolumeInstances(void *uniform _self,
    void *uniform instances,
    uniform uint32 numInstances)
{
  uniform World *uniform self = object_cast<World>(_self);
  self->volumeInstances = object_array_cast<Instance>(instances);
  self->numVolumeInstances = numInstances;
}

// The world-space light records, packed (sizeof_Light() bytes each)
export void World_setLights(void *uniform _self,
    const void *uniform lights,
    uniform uint32 numLights)
{
  uniform World *uniform self = object_cast<World>(_self);
  self->lights = (const uniform Light *uniform)lights;
  self->numLights = numLights;
}
