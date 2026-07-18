// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ParameterizedObject.h"
// std
#include <cstring>

namespace helium {

bool ParameterizedObject::hasParam(const std::string &name) const
{
  return findParam(name) != nullptr;
}

bool ParameterizedObject::hasParam(
    const std::string &name, ANARIDataType type) const
{
  auto *p = findParam(name);
  return p ? p->second.type() == type : false;
}

bool ParameterizedObject::setParam(
    const std::string &name, ANARIDataType type, const void *v)
{
  AnariAny value(type, v);
  auto *p = findParam(name);
  if (p->second != value) {
    p->second = value;
    return true;
  } else
    return false;
}

bool ParameterizedObject::getParam(
    const std::string &name, ANARIDataType type, void *v) const
{
  if (type == ANARI_STRING || anari::isObject(type))
    return false;

  auto *p = findParam(name);
  if (!p || !p->second.is(type))
    return false;

  std::memcpy(v, p->second.data(), anari::sizeOf(type));
  return true;
}

std::string ParameterizedObject::getParamString(
    const std::string &name, const std::string &valIfNotFound) const
{
  auto *p = findParam(name);
  return p ? p->second.getString() : valIfNotFound;
}

AnariAny ParameterizedObject::getParamDirect(const std::string &name) const
{
  auto *p = findParam(name);
  return p ? p->second : AnariAny();
}

void ParameterizedObject::setParamDirect(
    const std::string &name, const AnariAny &v)
{
  findParam(name)->second = v;
}

bool ParameterizedObject::removeParam(const std::string &name)
{
  auto foundParam = std::find_if(m_paramsStaging.begin(),
      m_paramsStaging.end(),
      [&](const Param &p) { return p.first == name; });

  if (foundParam != m_paramsStaging.end()) {
    m_paramsStaging.erase(foundParam);
    return true;
  } else
    return false;
}

bool ParameterizedObject::removeAllParams()
{
  if (!m_paramsStaging.empty()) {
    m_paramsStaging.clear();
    return true;
  } else
    return false;
}

void ParameterizedObject::commitParameterSnapshot()
{
  // Serialize against a deferred read (ReadCommittedScope) so the snapshot is
  // never overwritten while the flush is reading it. Vector copy-assignment
  // runs AnariAny's copy semantics per element, which release the previous
  // snapshot's object refs and acquire the new ones.
  std::lock_guard<std::mutex> guard(m_commitReadMutex);
  m_paramsCommitted = m_paramsStaging;
}

ParameterizedObject::ReadCommittedScope::ReadCommittedScope(
    ParameterizedObject *obj)
    : m_obj(obj)
{
  // Hold the snapshot mutex for the whole commit/finalize so a concurrent
  // commitParameterSnapshot() cannot mutate m_paramsCommitted mid-read.
  m_obj->m_commitReadMutex.lock();
  m_obj->m_readCommitted.store(true, std::memory_order_relaxed);
}

ParameterizedObject::ReadCommittedScope::~ReadCommittedScope()
{
  m_obj->m_readCommitted.store(false, std::memory_order_relaxed);
  m_obj->m_commitReadMutex.unlock();
}

ParameterizedObject::ParameterList::iterator ParameterizedObject::params_begin()
{
  return m_paramsStaging.begin();
}

ParameterizedObject::ParameterList::iterator ParameterizedObject::params_end()
{
  return m_paramsStaging.end();
}

const ParameterizedObject::ParameterList &ParameterizedObject::readParams()
    const
{
  return m_readCommitted.load(std::memory_order_relaxed) ? m_paramsCommitted
                                                         : m_paramsStaging;
}

const ParameterizedObject::Param *ParameterizedObject::findParam(
    const std::string &name) const
{
  const auto &params = readParams();
  auto foundParam = std::find_if(params.begin(),
      params.end(),
      [&](const Param &p) { return p.first == name; });

  if (foundParam != params.end())
    return &(*foundParam);
  return nullptr;
}

ParameterizedObject::Param *ParameterizedObject::findParam(
    const std::string &name)
{
  // Mutators always operate on the live staging store.
  auto foundParam = std::find_if(m_paramsStaging.begin(),
      m_paramsStaging.end(),
      [&](const Param &p) { return p.first == name; });

  if (foundParam != m_paramsStaging.end())
    return &(*foundParam);
  else {
    m_paramsStaging.emplace_back(name, AnariAny());
    return &m_paramsStaging[m_paramsStaging.size() - 1];
  }
}

} // namespace helium
