// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ParameterizedObject.h"
// std
#include <cstring>

namespace helium {

bool ParameterizedObject::hasParam(const std::string &name)
{
  return findParam(name, false) != nullptr;
}

void ParameterizedObject::setParam(
    const std::string &name, ANARIDataType type, const void *v)
{
  findParam(name, true)->second = AnariAny(type, v);
}

bool ParameterizedObject::getParam(
    const std::string &name, ANARIDataType type, void *v)
{
  if (type == ANARI_STRING || anari::isObject(type))
    return false;

  auto *p = findParam(name, false);
  if (!p || !p->second.is(type))
    return false;

  std::memcpy(v, p->second.data(), anari::sizeOf(type));
  return true;
}

std::string ParameterizedObject::getParamString(
    const std::string &name, const std::string &valIfNotFound)
{
  auto *p = findParam(name);
  return p ? p->second.getString() : valIfNotFound;
}

AnariAny ParameterizedObject::getParamDirect(const std::string &name)
{
  auto *p = findParam(name);
  return p ? p->second : AnariAny();
}

void ParameterizedObject::setParamDirect(
    const std::string &name, const AnariAny &v)
{
  findParam(name, true)->second = v;
}

void ParameterizedObject::removeParam(const std::string &name)
{
  auto foundParam = std::find_if(m_params.begin(),
      m_params.end(),
      [&](const Param &p) { return p.first == name; });

  if (foundParam != m_params.end())
    m_params.erase(foundParam);
}

void ParameterizedObject::removeAllParams()
{
  m_params.clear();
}

ParameterizedObject::ParameterList::iterator ParameterizedObject::params_begin()
{
  return m_params.begin();
}

ParameterizedObject::ParameterList::iterator ParameterizedObject::params_end()
{
  return m_params.end();
}

ParameterizedObject::Param *ParameterizedObject::findParam(
    const std::string &name, bool addIfNotExist)
{
  auto foundParam = std::find_if(m_params.begin(),
      m_params.end(),
      [&](const Param &p) { return p.first == name; });

  if (foundParam != m_params.end())
    return &(*foundParam);
  else if (addIfNotExist) {
    m_params.emplace_back(name, AnariAny());
    return &m_params[m_params.size() - 1];
  } else
    return nullptr;
}

} // namespace helium
