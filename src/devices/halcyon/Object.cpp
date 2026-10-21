// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Object.h"

namespace halcyon {

// Object definitions /////////////////////////////////////////////////////////

Object::Object(
    ANARIDataType type, HalcyonDeviceGlobalState *s, size_t ispcObjectSize)
    : helium::BaseObject(type, s)
{
  // slots never move, so their address can be kept for the object's lifetime
  auto allocate = [&](auto &slots) {
    auto slot = slots.emplace();
    m_ispcIndex = slot.index();
    m_ispcSlot = *slot;
  };

  switch (type) {
  case ANARI_RENDERER:
    allocate(s->registry.renderers);
    break;
  case ANARI_CAMERA:
    allocate(s->registry.cameras);
    break;
  case ANARI_GROUP:
    allocate(s->registry.groups);
    break;
  case ANARI_INSTANCE:
    allocate(s->registry.instances);
    break;
  case ANARI_WORLD:
    allocate(s->registry.worlds);
    break;
  case ANARI_SURFACE:
    allocate(s->registry.surfaces);
    break;
  case ANARI_MATERIAL:
    allocate(s->registry.materials);
    break;
  case ANARI_SAMPLER:
    allocate(s->registry.samplers);
    break;
  case ANARI_SPATIAL_FIELD:
    allocate(s->registry.spatialFields);
    break;
  case ANARI_VOLUME:
    allocate(s->registry.volumes);
    break;
  case ANARI_GEOMETRY:
    allocate(s->registry.geometries);
    break;
  default:
    break;
  }

  // types without an ISPC registry slot have nothing to check
  if (m_ispcIndex == INVALID_INDEX)
    return;

  auto ispcStorageSize = objectSize();
  if (ispcStorageSize > 0 && ispcObjectSize != ispcStorageSize) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "size mismatch for type '%s' --> "
        "sizeof(ispc::[Object]) == %zu vs. sizeof(ispcStorage) == %zu",
        anari::toString(type),
        ispcObjectSize,
        ispcStorageSize);
  }
}

Object::~Object()
{
  auto &state = *deviceState();

  auto idx = objectIndex();
  if (idx == INVALID_INDEX)
    return;

  switch (type()) {
  case ANARI_RENDERER:
    state.registry.renderers.erase(idx);
    break;
  case ANARI_CAMERA:
    state.registry.cameras.erase(idx);
    break;
  case ANARI_GROUP:
    state.registry.groups.erase(idx);
    break;
  case ANARI_INSTANCE:
    state.registry.instances.erase(idx);
    break;
  case ANARI_WORLD:
    state.registry.worlds.erase(idx);
    break;
  case ANARI_SURFACE:
    state.registry.surfaces.erase(idx);
    break;
  case ANARI_MATERIAL:
    state.registry.materials.erase(idx);
    break;
  case ANARI_SAMPLER:
    state.registry.samplers.erase(idx);
    break;
  case ANARI_SPATIAL_FIELD:
    state.registry.spatialFields.erase(idx);
    break;
  case ANARI_VOLUME:
    state.registry.volumes.erase(idx);
    break;
  case ANARI_GEOMETRY:
    state.registry.geometries.erase(idx);
    break;
  default:
    break;
  }
}

void Object::commitParameters()
{
  // no-op
}

void Object::finalize()
{
  // no-op
}

bool Object::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (name == "valid" && type == ANARI_BOOL) {
    helium::writeToVoidP(ptr, isValid());
    return true;
  }

  return false;
}

bool Object::isValid() const
{
  return true;
}

HalcyonDeviceGlobalState *Object::deviceState() const
{
  return (HalcyonDeviceGlobalState *)helium::BaseObject::m_state;
}

void *Object::getIE() const
{
  return m_ispcSlot;
}

size_t Object::objectIndex() const
{
  return m_ispcIndex;
}

size_t Object::objectSize() const
{
  auto &state = *deviceState();

  auto idx = objectIndex();
  if (idx == INVALID_INDEX) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "queried object size for unallocated ISPC object for type %s",
        anari::toString(type()));
    return 0;
  }

  switch (type()) {
  case ANARI_RENDERER:
    return state.registry.renderers[idx].size();
  case ANARI_CAMERA:
    return state.registry.cameras[idx].size();
  case ANARI_GROUP:
    return state.registry.groups[idx].size();
  case ANARI_INSTANCE:
    return state.registry.instances[idx].size();
  case ANARI_WORLD:
    return state.registry.worlds[idx].size();
  case ANARI_SURFACE:
    return state.registry.surfaces[idx].size();
  case ANARI_MATERIAL:
    return state.registry.materials[idx].size();
  case ANARI_SAMPLER:
    return state.registry.samplers[idx].size();
  case ANARI_SPATIAL_FIELD:
    return state.registry.spatialFields[idx].size();
  case ANARI_VOLUME:
    return state.registry.volumes[idx].size();
  case ANARI_GEOMETRY:
    return state.registry.geometries[idx].size();
  default:
    break;
  }

  return 0;
}

// UnknownObject definitions //////////////////////////////////////////////////

UnknownObject::UnknownObject(
    ANARIDataType type, HalcyonDeviceGlobalState *s, size_t ispcObjectSize)
    : Object(type, s, ispcObjectSize)
{}

bool UnknownObject::isValid() const
{
  return false;
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Object *);
