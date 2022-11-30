#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <memory>

#include <pybind11/pybind11.h>

#include <anari/anari.h>
#include "anari/anari_feature_utility.h"
#include "SceneGenerator.h"

namespace cts {
void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message);

std::string getDefaultDeviceName(const std::string &libraryName,
    const std::optional<pybind11::function>
        &callback);

std::vector<std::tuple<std::string, bool>> queryFeatures(
    const std::string &libraryName,
    const std::optional<std::string> &device,
    const std::optional<pybind11::function>
        &callback);

// SceneGeneratorWrapper wraps the SceneGenerator functionality
// to provide safe access via pybind
class SceneGeneratorWrapper
{
 public:
  SceneGeneratorWrapper(const std::string &library,
      const std::optional<std::string> &device,
      const std::optional<pybind11::function> &callback);
  ~SceneGeneratorWrapper();

  void commit()
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->commit();
    }
  }

  void resetSceneObjects()
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->resetSceneObjects();
    }
  }

  void resetAllParameters()
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->resetAllParameters();
    }
  }

  std::vector<std::vector<uint32_t>> renderScene(
      const std::string& rendererType, float renderDistance)
  {
    if (m_sceneGenerator) {
      return m_sceneGenerator->renderScene(rendererType, renderDistance);
    }
    return {};
  }

  std::vector<std::vector<std::vector<std::vector<float>>>> getBounds() {
    if (m_sceneGenerator) {
      return m_sceneGenerator->getBounds();
    }
    return {};
  }

  float getFrameDuration() const {
    if (m_sceneGenerator) {
      return m_sceneGenerator->getFrameDuration();
    }
    return -1.0f;
  }

  template <typename T>
  void setParam(const std::string& name, const T& t)
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->setParam(name, t);
    }
  }

 private:
  std::unique_ptr<SceneGenerator> m_sceneGenerator;
  anari::Library m_library;
  std::optional<pybind11::function> m_callback;
};
} // namespace cts
