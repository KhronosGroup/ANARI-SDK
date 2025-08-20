// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "debugCodes.h"
#include "geometry.h"
#include "hdAnariTypes.h"
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
#include <mutex>
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
      HdAnariMaterial::MaterialType materialType = HdAnariMaterial::MaterialType::PhysicallyBased);
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

 private:
  anari::Device _device{nullptr};
  anari::Material _material{nullptr};
  HdAnariMaterial::MaterialType _materialType{HdAnariMaterial::MaterialType::Matte};
  HdAnariMaterial::PrimvarBinding _primvarBinding;

  std::mutex _mutex;
  GeometryList _geometries;
  LightList _lights;
  std::atomic<int> _sceneVersion{0};
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

  anari::release(_device, _material);
  anari::release(_device, _device);
  _device = nullptr;
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

PXR_NAMESPACE_CLOSE_SCOPE
