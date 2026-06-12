// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anariTypes.h"
#include "debugCodes.h"
#include "geometry.h"
#include "light.h"
#include "material.h"
#include "materialTokens.h"

#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>

// pxr
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/hashmap.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderThread.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/pathTable.h>

// std
#include <algorithm>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
#include <atomic>
#include <cassert>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariGeometry;
using GeometryList = std::vector<const HdAnariGeometry *>;

class HdAnariLight;
using LightList = std::vector<const HdAnariLight *>;

class HdAnariRenderParam final : public HdRenderParam
{
 public:
  HdAnariRenderParam(anari::Device device,
      HdAnariMaterial::MaterialType materialType =
          HdAnariMaterial::MaterialType::PhysicallyBased);
  ~HdAnariRenderParam() override;

  anari::Device GetANARIDevice() const;
  anari::Material GetDefaultMaterial() const;
  const HdAnariMaterial::PrimvarBinding &GetDefaultPrimvarBinding() const;

  HdAnariMaterial::MaterialType GetMaterialType() const
  {
    return _materialType;
  }

  void RegisterGeometry(const HdAnariGeometry *geometry);
  void UnregisterGeometry(const HdAnariGeometry *geometry);
  void RegisterLight(const HdAnariLight *light);
  void UnregisterLight(const HdAnariLight *light);

  const GeometryList &Geometries() const;
  const LightList &Lights() const;

  int SceneVersion() const;
  void MarkNewSceneVersion();

  // Accumulation progress, published by the render pass and read back by the
  // render delegate's GetRenderStats().
  void SetProgress(int completedSamples, int sampleLimit);
  int CompletedSamples() const;
  int SampleLimit() const;

  // Share one anari material across instances whose content (MDL source +
  // parameters) is identical, so the device only compiles it and loads its
  // textures once. `key` identifies the content; `create` builds the material
  // on the first request. Balanced by ReleaseMdlMaterial.
  anari::Material AcquireMdlMaterial(
      const std::string &key, const std::function<anari::Material()> &create);
  void ReleaseMdlMaterial(const std::string &key);

 private:
  anari::Device _device{nullptr};
  anari::Material _material{nullptr};
  HdAnariMaterial::MaterialType _materialType{
      HdAnariMaterial::MaterialType::Matte};
  HdAnariMaterial::PrimvarBinding _primvarBinding;

  struct MdlMaterialCacheEntry
  {
    anari::Material material;
    int refCount;
  };
  std::mutex _mdlMaterialMutex;
  std::unordered_map<std::string, MdlMaterialCacheEntry> _mdlMaterialCache;

  std::mutex _mutex;
  GeometryList _geometries;
  LightList _lights;
  std::atomic<int> _sceneVersion{0};
  std::atomic<int> _completedSamples{0};
  std::atomic<int> _sampleLimit{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline HdAnariRenderParam::HdAnariRenderParam(
    anari::Device d, HdAnariMaterial::MaterialType materialType)
    : _device(d), _materialType(materialType)
{
  if (!d)
    return;

  // Always go with white matte as a fallback material.
  _material = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, _material, "alphaMode", "opaque");
  anari::setParameter(
      d, _material, "color", GfVec3f(0.8f, 0.8f, 0.8f)); // "color");
  anari::commitParameters(d, _material);

  _primvarBinding.emplace(HdTokens->displayColor, "color");
}

inline HdAnariRenderParam::~HdAnariRenderParam()
{
  if (!_device)
    return;

  for (auto &entry : _mdlMaterialCache)
    anari::release(_device, entry.second.material);
  _mdlMaterialCache.clear();

  anari::release(_device, _material);
  anari::release(_device, _device);
  _device = nullptr;
}

inline anari::Material HdAnariRenderParam::AcquireMdlMaterial(
    const std::string &key, const std::function<anari::Material()> &create)
{
  std::lock_guard<std::mutex> guard(_mdlMaterialMutex);
  auto it = _mdlMaterialCache.find(key);
  if (it == cend(_mdlMaterialCache)) {
    auto material = create();
    if (!material)
      return nullptr; // don't cache a failed creation and poison this key
    it = _mdlMaterialCache.insert({key, {material, 0}}).first;
  }
  ++it->second.refCount;
  return it->second.material;
}

inline void HdAnariRenderParam::ReleaseMdlMaterial(const std::string &key)
{
  std::lock_guard<std::mutex> guard(_mdlMaterialMutex);
  auto it = _mdlMaterialCache.find(key);
  if (it == cend(_mdlMaterialCache) || it->second.refCount == 0)
    return;
  if (--it->second.refCount == 0) {
    anari::release(_device, it->second.material);
    _mdlMaterialCache.erase(it);
  }
}

inline anari::Device HdAnariRenderParam::GetANARIDevice() const
{
  assert(_device != nullptr);
  return _device;
}

inline anari::Material HdAnariRenderParam::GetDefaultMaterial() const
{
  return _material;
}

inline const HdAnariMaterial::PrimvarBinding &
HdAnariRenderParam::GetDefaultPrimvarBinding() const
{
  return _primvarBinding;
}

inline void HdAnariRenderParam::RegisterGeometry(
    const HdAnariGeometry *geometry)
{
  std::lock_guard<std::mutex> guard(_mutex);
  _geometries.push_back(geometry);
}

inline void HdAnariRenderParam::UnregisterGeometry(
    const HdAnariGeometry *geometry)
{
  std::lock_guard<std::mutex> guard(_mutex);
  _geometries.erase(
      std::remove(_geometries.begin(), _geometries.end(), geometry),
      _geometries.end());
}

inline void HdAnariRenderParam::RegisterLight(const HdAnariLight *light)
{
  std::lock_guard<std::mutex> guard(_mutex);
  _lights.push_back(light);
}

inline void HdAnariRenderParam::UnregisterLight(const HdAnariLight *light)
{
  std::lock_guard<std::mutex> guard(_mutex);
  _lights.erase(
      std::remove(_lights.begin(), _lights.end(), light), _lights.end());
}

inline const GeometryList &HdAnariRenderParam::Geometries() const
{
  return _geometries;
}

inline const LightList &HdAnariRenderParam::Lights() const
{
  return _lights;
}

inline int HdAnariRenderParam::SceneVersion() const
{
  return _sceneVersion.load();
}

inline void HdAnariRenderParam::MarkNewSceneVersion()
{
  _sceneVersion++;
}

inline void HdAnariRenderParam::SetProgress(
    int completedSamples, int sampleLimit)
{
  _completedSamples.store(completedSamples);
  _sampleLimit.store(sampleLimit);
}

inline int HdAnariRenderParam::CompletedSamples() const
{
  return _completedSamples.load();
}

inline int HdAnariRenderParam::SampleLimit() const
{
  return _sampleLimit.load();
}

PXR_NAMESPACE_CLOSE_SCOPE
