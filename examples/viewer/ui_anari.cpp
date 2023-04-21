// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ui_anari.h"

namespace ui {

// Helper functions ///////////////////////////////////////////////////////////

static ui::Any parseValue(ANARIDataType type, const void *mem)
{
  if (type == ANARI_STRING)
    return ui::Any(ANARI_STRING, "");
  else if (anari::isObject(type)) {
    ANARIObject nullHandle = ANARI_INVALID_HANDLE;
    return ui::Any(type, &nullHandle);
  } else if (mem)
    return ui::Any(type, mem);
  else
    return {};
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

ParameterList parseParameters(
    anari::Library l, ANARIDataType objectType, const char *subtype)
{
  ParameterList retval;

  auto *parameter = (const ANARIParameter *)anariGetObjectInfo(
      l, "default", subtype, objectType, "parameter", ANARI_PARAMETER_LIST);

  for (; parameter && parameter->name != nullptr; parameter++) {
    Parameter p;

    auto *description = (const char *)anariGetParameterInfo(l,
        "default",
        subtype,
        objectType,
        parameter->name,
        parameter->type,
        "description",
        ANARI_STRING);

    const void *defaultValue = anariGetParameterInfo(l,
        "default",
        subtype,
        objectType,
        parameter->name,
        parameter->type,
        "default",
        parameter->type);

    p.name = parameter->name;
    p.description = description ? description : "";
    p.value = parseValue(parameter->type, defaultValue);
    retval.push_back(p);
  }

  return retval;
}

void buildUI(anari::Device d, anari::Object o, Parameter &p)
{
  bool update = false;

  ANARIDataType type = p.value.type();
  const char *name = p.name.c_str();
  void *value = p.value.data();

  switch (type) {
  case ANARI_BOOL:
    update = ImGui::Checkbox(name, (bool *)value);
    break;
  case ANARI_INT32:
    update = ImGui::DragInt(name, (int *)value);
    break;
  case ANARI_FLOAT32:
    update = ImGui::InputFloat(name, (float *)value);
    break;
  case ANARI_FLOAT32_VEC2:
    update = ImGui::InputFloat2(name, (float *)value);
    break;
  case ANARI_FLOAT32_VEC3:
    update = ImGui::InputFloat3(name, (float *)value);
    break;
  case ANARI_FLOAT32_VEC4:
    update = ImGui::InputFloat4(name, (float *)value);
    break;
  default:
    ImGui::Text("* %s | %s", name, anari::toString(type));
    break;
  }

  if (update) {
    if (p.value.type() == ANARI_STRING)
      anari::setParameter(d, o, name, p.value.getString());
    else
      anari::setParameter(d, o, name, type, value);
    anari::commitParameters(d, o);
  }
}

} // namespace ui
