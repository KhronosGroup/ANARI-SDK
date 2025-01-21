// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SceneSelector.h"
// anari_viewer
#include "anari_viewer/ui_anari.h"

namespace anari_viewer::windows {

static bool UI_callback(void *_stringList, int index, const char **out_text)
{
  auto &stringList = *(std::vector<std::string> *)_stringList;
  *out_text = stringList[index].c_str();
  return true;
}

static void buildUISceneHandle(
    anari::scenes::SceneHandle s, helium::ParameterInfo &p)
{
  if (ui::buildUI(p))
    anari::scenes::setParameter(s, p.name, p.value);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

SceneSelector::SceneSelector(Application *app, const char *name) : Window(app, name, true)
{
  m_categories = anari::scenes::getAvailableSceneCategories();
  m_scenes.resize(m_categories.size());
  for (int i = 0; i < m_categories.size(); i++) {
    m_scenes[i] =
        anari::scenes::getAvailableSceneNames(m_categories[i].c_str());
  }
}

SceneSelector::~SceneSelector()
{
  anari::scenes::release(m_scene);
}

void SceneSelector::buildUI()
{
  bool newCategory = ImGui::Combo("category##whichCategory",
      &m_currentCategory,
      UI_callback,
      &m_categories,
      m_categories.size());

  if (newCategory)
    m_currentScene = 0;

  auto &sceneList = m_scenes[m_currentCategory];

  bool newScene = ImGui::Combo("scene##whichScene",
      &m_currentScene,
      UI_callback,
      &sceneList,
      sceneList.size());

  if (newCategory || newScene)
    notify();

  ImGui::Separator();

  if (m_parameters.empty())
    return;

  for (auto &p : m_parameters)
    buildUISceneHandle(m_scene, p);

  ImGui::NewLine();

  if (ImGui::Button("update")) {
    try {
      anari::scenes::commit(m_scene);
    } catch (const std::runtime_error &e) {
      printf("%s\n", e.what());
    }
  }
}

void SceneSelector::setCallback(SceneSelectionCallback cb)
{
  m_callback = cb;
  notify();
}

void SceneSelector::setScene(anari::scenes::SceneHandle scene)
{
  anari::scenes::release(m_scene);
  m_scene = scene;
  m_parameters = getParameters(scene);
}

void SceneSelector::notify()
{
  if (m_callback) {
    const char *c = m_categories[m_currentCategory].c_str();
    const char *s = m_scenes[m_currentCategory][m_currentScene].c_str();
    m_callback(c, s);
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

} // namespace anari_viewer::windows
