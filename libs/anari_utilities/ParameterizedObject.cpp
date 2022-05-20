// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/backend/utilities/ParameterizedObject.h"

#include <algorithm>

namespace anari {

ParameterizedObject::Param::Param(const std::string &_name) : name(_name) {}

void ParameterizedObject::Param::setDirect(Any v)
{
  data = v;
  locallyStored = false;
  type = ANARI_UNKNOWN;
}

Any ParameterizedObject::getParamDirect(const std::string &name)
{
  Param *p = findParam(name);
  if (!p)
    throw std::runtime_error("cannot retrieve missing parameter");
  if (p->locallyStored)
    throw std::runtime_error("cannot get locally stored parameter directly");
  return p->data;
}

void ParameterizedObject::setParamDirect(const std::string &name, Any v)
{
  findParam(name, true)->setDirect(v);
}

void ParameterizedObject::removeParam(const std::string &name)
{
  auto foundParam = std::find_if(paramList.begin(),
      paramList.end(),
      [&](const std::shared_ptr<Param> &p) { return p->name == name; });

  if (foundParam != paramList.end()) {
    paramList.erase(foundParam);
  }
}

ParameterizedObject::Param *ParameterizedObject::findParam(
    const std::string &name, bool addIfNotExist)
{
  auto foundParam = std::find_if(paramList.begin(),
      paramList.end(),
      [&](const std::shared_ptr<Param> &p) { return p->name == name; });

  if (foundParam != paramList.end())
    return foundParam->get();
  else if (addIfNotExist) {
    paramList.push_back(std::make_shared<Param>(name));
    return paramList[paramList.size() - 1].get();
  } else
    return nullptr;
}

} // namespace anari
