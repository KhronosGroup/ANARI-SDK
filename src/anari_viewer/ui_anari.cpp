// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ui_anari.h"
// anari_viewer
#include "nfd.h"
// std
#include <limits>

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

static bool UI_stringList_callback(
    void *_stringList, int index, const char **out_text)
{
  auto &stringList = *(std::vector<std::string> *)_stringList;
  *out_text = stringList[index].c_str();
  return true;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void init()
{
  NFD_Init();
}

void shutdown()
{
  NFD_Quit();
}

ParameterList parseParameters(
    anari::Device d, ANARIDataType objectType, const char *subtype)
{
  ParameterList retval;

  auto *parameter = (const ANARIParameter *)anariGetObjectInfo(
      d, objectType, subtype, "parameter", ANARI_PARAMETER_LIST);

  for (; parameter && parameter->name != nullptr; parameter++) {
    Parameter p;

    auto *description = (const char *)anariGetParameterInfo(d,
        objectType,
        subtype,
        parameter->name,
        parameter->type,
        "description",
        ANARI_STRING);

    const void *defaultValue = anariGetParameterInfo(d,
        objectType,
        subtype,
        parameter->name,
        parameter->type,
        "default",
        parameter->type);

    const void *minValue = anariGetParameterInfo(d,
        objectType,
        subtype,
        parameter->name,
        parameter->type,
        "minimum",
        parameter->type);

    const void *maxValue = anariGetParameterInfo(d,
        objectType,
        subtype,
        parameter->name,
        parameter->type,
        "maximum",
        parameter->type);

    const auto **stringValues = (const char **)anariGetParameterInfo(d,
        objectType,
        subtype,
        parameter->name,
        parameter->type,
        "value",
        ANARI_STRING_LIST);

    p.name = parameter->name;
    p.description = description ? description : "";
    p.value = parseValue(parameter->type, defaultValue);

    if (minValue)
      p.min = parseValue(parameter->type, minValue);
    if (maxValue)
      p.max = parseValue(parameter->type, maxValue);

    for (; stringValues && *stringValues; stringValues++)
      p.stringValues.push_back(*stringValues);

    retval.push_back(p);
  }

  return retval;
}

bool buildUI(Parameter &p)
{
  bool update = false;

  ANARIDataType type = p.value.type();
  const char *name = p.name.c_str();
  void *value = p.value.data();

  const bool bounded = p.min || p.max;
  const bool showTooltip = !p.description.empty();

  switch (type) {
  case ANARI_BOOL:
    update |= ImGui::Checkbox(name, (bool *)value);
    break;
  case ANARI_INT32:
    if (bounded) {
      if (p.min && p.max) {
        update |= ImGui::SliderInt(
            name, (int *)value, p.min.get<int>(), p.max.get<int>());
      } else {
        int min = p.min ? p.min.get<int>() : std::numeric_limits<int>::lowest();
        int max = p.max ? p.max.get<int>() : std::numeric_limits<int>::max();
        update |= ImGui::DragInt(name, (int *)value, 1.f, min, max);
      }
    } else
      update |= ImGui::InputInt(name, (int *)value);
    break;
  case ANARI_FLOAT32:
    if (bounded) {
      if (p.min && p.max) {
        update |= ImGui::SliderFloat(
            name, (float *)value, p.min.get<float>(), p.max.get<float>());
      } else {
        float min =
            p.min ? p.min.get<float>() : std::numeric_limits<float>::lowest();
        float max =
            p.max ? p.max.get<float>() : std::numeric_limits<float>::max();
        update |= ImGui::DragFloat(name, (float *)value, 1.f, min, max);
      }
    } else
      update |= ImGui::InputFloat(name, (float *)value);
    break;
  case ANARI_FLOAT32_VEC2:
    update |= ImGui::InputFloat2(name, (float *)value);
    break;
  case ANARI_FLOAT32_VEC3:
    update |= ImGui::InputFloat3(name, (float *)value);
    break;
  case ANARI_FLOAT32_VEC4:
    update |= ImGui::ColorEdit4(name, (float *)value); // TODO handle non-colors
    break;
  case ANARI_STRING: {
    if (!p.stringValues.empty()) {
      update |= ImGui::Combo(name,
          &p.currentSelection,
          UI_stringList_callback,
          &p.stringValues,
          p.stringValues.size());

      if (update)
        p.value = p.stringValues[p.currentSelection].c_str();
    } else {
      constexpr int MAX_LENGTH = 2000;
      p.value.reserveString(MAX_LENGTH);

      if (ImGui::Button("...")) {
        nfdchar_t *outPath = nullptr;
        nfdfilteritem_t filterItem[2] = {
            {"OBJ Files", "obj"}, {"glTF Files", "gltf,glb"}};
        nfdresult_t result = NFD_OpenDialog(&outPath, filterItem, 2, nullptr);
        if (result == NFD_OKAY) {
          p.value = std::string(outPath).c_str();
          update = true;
          NFD_FreePath(outPath);
        } else {
          printf("NFD Error: %s\n", NFD_GetError());
        }
      }

      ImGui::SameLine();

      auto text_cb = [](ImGuiInputTextCallbackData *cbd) {
        auto &p = *(ui::Parameter *)cbd->UserData;
        p.value.resizeString(cbd->BufTextLen);
        return 0;
      };
      update |= ImGui::InputText(name,
          (char *)value,
          MAX_LENGTH,
          ImGuiInputTextFlags_CallbackEdit,
          text_cb,
          &p);
    }
  } break;
  default:
    ImGui::Text("* %s | %s", name, anari::toString(type));
    break;
  }

  if (!p.description.empty() && ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("%s", p.description.c_str());
    ImGui::EndTooltip();
  }

  return update;
}

void buildUI(anari::scenes::SceneHandle s, Parameter &p)
{
  if (buildUI(p))
    anari::scenes::setParameter(s, p.name, p.value);
}

void buildUI(anari::Device d, anari::Object o, Parameter &p)
{
  ANARIDataType type = p.value.type();
  const char *name = p.name.c_str();
  void *value = p.value.data();

  if (buildUI(p)) {
    if (p.value.type() == ANARI_STRING)
      anari::setParameter(d, o, name, p.value.getString());
    else
      anari::setParameter(d, o, name, type, value);
    anari::commitParameters(d, o);
  }
}

} // namespace ui
