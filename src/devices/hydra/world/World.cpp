// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "World.h"

#include <map>

namespace anari_hydra {

World::World(HydraGlobalState *s) : Object(ANARI_WORLD, s)
{
  m_serial = s->nextWorldSerial++;
  m_mirror = std::make_unique<Mirror>(m_serial);
}

bool World::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  return QueryBounds(*this, m_bounds, name, type, ptr, size, flags);
}

Bounds World::bounds(bool proxy) const
{
  Bounds result;
  for (const auto &r : gatherRenderables())
    result.UnionWith(r.surface->geometry()->bounds());
  for (auto *v : gatherVolumes())
    result.UnionWith(v->bounds(proxy));
  for (const auto &group : gatherInstancedGroups()) {
    const Bounds local = group.group->bounds(proxy);
    if (local.IsEmpty())
      continue;
    for (const auto &transform : group.transforms) {
      for (int corner = 0; corner < 8; ++corner) {
        helium::float4 p;
        for (int i = 0; i < 3; ++i)
          p[i] = (corner & (1 << i)) ? local.GetMax()[i] : local.GetMin()[i];
        p.w = 1.f;
        const auto world = linalg::mul(transform, p);
        result.UnionWith(pxr::GfVec3f(world.x, world.y, world.z));
      }
    }
  }
  return result;
}

void World::commitParameters()
{
  m_instance = getParamObject<helium::ObjectArray>("instance");
  m_surface = getParamObject<helium::ObjectArray>("surface");
  m_light = getParamObject<helium::ObjectArray>("light");
  m_volume = getParamObject<helium::ObjectArray>("volume");
}

void World::finalize()
{
  deviceState()->mirrorDirty.mark(this);
}

std::vector<World::Renderable> World::gatherRenderables() const
{
  std::vector<Renderable> out;

  // World-direct surfaces: identity transform, no instancer.
  if (m_surface) {
    for (auto *h = m_surface->handlesBegin(); h != m_surface->handlesEnd();
         ++h) {
      auto *s = dynamic_cast<Surface *>(*h);
      if (!s || !s->isValid())
        continue;
      Renderable r;
      r.surfaceSerial = s->serial();
      r.surface = s;
      out.push_back(r);
    }
  }

  return out;
}

std::vector<World::InstancedGroup> World::gatherInstancedGroups() const
{
  // Aggregate every instance by the group it references: one HdInstancer per
  // group, one flattened transform per (instance, transform element). Preserves
  // first-seen group order for stable prim paths.
  std::vector<InstancedGroup> out;
  std::map<Group *, size_t> indexOf;
  if (!m_instance)
    return out;

  for (auto *h = m_instance->handlesBegin(); h != m_instance->handlesEnd();
       ++h) {
    auto *inst = dynamic_cast<Instance *>(*h);
    if (!inst || !inst->isValid())
      continue;
    Group *g = inst->group();
    if (!g)
      continue;
    auto it = indexOf.find(g);
    if (it == indexOf.end()) {
      it = indexOf.emplace(g, out.size()).first;
      out.push_back({g, {}, {}, {}, {}, {}});
    }
    InstancedGroup &ig = out[it->second];
    const bool array = inst->hasTransformArray();
    const size_t n = inst->transformCount();
    for (size_t i = 0; i < n; ++i) {
      ig.transforms.push_back(inst->transform(i));
      ig.instanceSerials.push_back(inst->serial());
      ig.elements.push_back(array ? int(i) : -1);
      ig.overrides.push_back(inst->overrides(i));
      ig.ids.push_back(inst->id(i));
    }
  }

  return out;
}

std::vector<Light *> World::gatherLights() const
{
  std::vector<Light *> out;
  if (!m_light)
    return out;
  for (auto *h = m_light->handlesBegin(); h != m_light->handlesEnd(); ++h) {
    if (auto *l = dynamic_cast<Light *>(*h))
      out.push_back(l);
  }
  return out;
}

std::vector<Volume *> World::gatherVolumes() const
{
  std::vector<Volume *> out;
  if (!m_volume)
    return out;
  for (auto *h = m_volume->handlesBegin(); h != m_volume->handlesEnd(); ++h) {
    if (auto *v = dynamic_cast<Volume *>(*h))
      out.push_back(v);
  }
  return out;
}

bool World::hasAnyLight() const
{
  if (!gatherLights().empty())
    return true;
  // An instanced group carrying lights expands to per-instance sprims, which
  // count as lights even with no world-direct light present.
  for (const auto &ig : gatherInstancedGroups()) {
    if (!ig.group->lights().empty())
      return true;
  }
  return false;
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::World *);
}
