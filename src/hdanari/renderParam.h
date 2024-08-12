// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp.hpp>

#include "debugCodes.h"
#include "hdAnariTypes.h"

// pxr
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
#include <atomic>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariMesh;
using MeshList = std::vector<const HdAnariMesh *>;

struct HdAnariRenderParam final : public HdRenderParam
{
  enum class MaterialType
  {
    Matte,
    PhysicallyBased,
  };

  HdAnariRenderParam(anari::Device device);
  ~HdAnariRenderParam() override;

  anari::Device GetANARIDevice() const;
  anari::Material GetANARIDefaultMaterial() const;

  MaterialType GetMaterialType() const
  {
    return _materialType;
  }

  void AddMesh(HdAnariMesh *m);
  const MeshList &Meshes() const;
  void RemoveMesh(HdAnariMesh *m);

  int SceneVersion() const;
  void MarkNewSceneVersion();

 private:
  anari::Device _device{nullptr};
  anari::Material _material{nullptr};
  MaterialType _materialType{MaterialType::Matte};

  std::mutex _mutex;
  MeshList _meshes;
  std::atomic<int> _sceneVersion{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline HdAnariRenderParam::HdAnariRenderParam(anari::Device d) : _device(d)
{
  if (!d)
    return;

  anari::Extensions extensions =
      anari::extension::getInstanceExtensionStruct(d, d);

  if (extensions.ANARI_KHR_MATERIAL_PHYSICALLY_BASED) {
    _materialType = MaterialType::PhysicallyBased;
    _material = anari::newObject<anari::Material>(d, "physicallyBased");
    anari::setParameter(d, _material, "alphaMode", "opaque");
    anari::setParameter(d, _material, "baseColor", "color");
    anari::commitParameters(d, _material);
  } else {
    _materialType = MaterialType::Matte;
    _material = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, _material, "alphaMode", "opaque");
    anari::setParameter(d, _material, "color", "color");
    anari::commitParameters(d, _material);
  }
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

inline anari::Material HdAnariRenderParam::GetANARIDefaultMaterial() const
{
  return _material;
}

inline void HdAnariRenderParam::AddMesh(HdAnariMesh *m)
{
  std::lock_guard<std::mutex> guard(_mutex);
  _meshes.push_back(m);
}

inline const MeshList &HdAnariRenderParam::Meshes() const
{
  return _meshes;
}

inline void HdAnariRenderParam::RemoveMesh(HdAnariMesh *m)
{
  std::lock_guard<std::mutex> guard(_mutex);
  _meshes.erase(std::remove(_meshes.begin(), _meshes.end(), m), _meshes.end());
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
