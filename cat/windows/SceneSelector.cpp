// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SceneSelector.h"
#include "PerformanceMetrics.h"
#include "anari_test_scenes.h"
#include "anari_test_scenes/scenes/scene.h"
#include "anari_viewer/ui_anari.h"

namespace anari_cat::windows {

static bool UI_callback(void *_stringList, int index, const char **out_text)
{
  auto &stringList = *(std::vector<std::string> *)_stringList;
  *out_text = stringList[index].c_str();
  return true;
}

static void buildUISceneHandle(
    anari::scenes::SceneHandle s, helium::ParameterInfo &p)
{
  if (anari_viewer::ui::buildUI(p))
    anari::scenes::setParameter(s, p.name, p.value);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

SceneSelector::SceneSelector(const char *name,
    int currentCategory /*=0*/,
    int currentScene /*=0*/,
    bool initAnimate /*=false*/)
    : Window(name, true),
      m_currentCategory(currentCategory),
      m_currentScene(currentScene),
      m_animate(initAnimate)
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
      static_cast<int>(m_categories.size()));

  if (newCategory)
    m_currentScene = 0;

  auto &sceneList = m_scenes[m_currentCategory];

  bool newScene = ImGui::Combo("scene##whichScene",
      &m_currentScene,
      UI_callback,
      &sceneList,
      static_cast<int>(sceneList.size()));

  if (newCategory || newScene)
    notify();

  ImGui::Separator();

  if (m_parameters.empty())
    return;

  if (anari::scenes::isAnimated(m_scene)) {
    if (ImGui::Button(m_animate ? "Pause" : "Play")) {
      m_animate = !m_animate;
    }
    if (m_animate) {
      try {
        // measure incremental updates
        if (m_perfRecorder) {
          m_perfRecorder->StartTimer(anari_cat::TIME_SCENE_UPDATE);
        }
        anari::scenes::computeNextFrame(m_scene);
        if (m_perfRecorder) {
          m_perfRecorder->StopTimer(anari_cat::TIME_SCENE_UPDATE);
        }
        // Some scenes might change parameters in computeNextFrame.
        // Sync parameters from scene with the values displayed in UI
        for (auto &p : m_parameters) {
          if (p.value.is(ANARI_STRING)) {
            const auto value = m_scene->getParamString(p.name, "");
            p.value = value.c_str();
          } else {
            m_scene->getParam(p.name, p.value.type(), p.value.data());
          }
        }
      } catch (const std::runtime_error &e) {
        printf("%s\n", e.what());
      }
    }
  }

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

} // namespace anari_cat::windows
