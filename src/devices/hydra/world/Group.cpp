// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Group.h"

namespace anari_hydra {

Group::Group(HydraGlobalState *s) : Object(ANARI_GROUP, s)
{
  m_serial = s->nextSerial++;
}

bool Group::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  return QueryBounds(*this, m_bounds, name, type, ptr, size, flags);
}

Bounds Group::bounds(bool proxy) const
{
  Bounds result;
  for (auto *surface : surfaces())
    if (surface->isValid())
      result.UnionWith(surface->geometry()->bounds());
  for (auto *volume : volumes())
    result.UnionWith(volume->bounds(proxy));
  return result;
}

void Group::commitParameters()
{
  m_surface = getParamObject<helium::ObjectArray>("surface");
  m_light = getParamObject<helium::ObjectArray>("light");
  m_volume = getParamObject<helium::ObjectArray>("volume");
}

void Group::finalize()
{
  deviceState()->mirrorDirty.mark(this);
}

std::vector<Surface *> Group::surfaces() const
{
  std::vector<Surface *> out;
  if (!m_surface)
    return out;
  for (auto *h = m_surface->handlesBegin(); h != m_surface->handlesEnd(); ++h) {
    if (auto *s = dynamic_cast<Surface *>(*h))
      out.push_back(s);
  }
  return out;
}

std::vector<Light *> Group::lights() const
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

std::vector<Volume *> Group::volumes() const
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

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Group *);
}
