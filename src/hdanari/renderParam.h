// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "hdAnariTypes.h"
// pxr
#include <pxr/base/tf/debug.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderThread.h>
#include <pxr/pxr.h>
// std
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariMesh;
using MeshList = std::vector<const HdAnariMesh *>;

struct HdAnariRenderParam final : public HdRenderParam
{
  HdAnariRenderParam(anari::Device device);
  ~HdAnariRenderParam() override;

  anari::Device GetANARIDevice() const;
  anari::Material GetANARIDefaultMaterial() const;

  void AddMesh(HdAnariMesh *m);
  const MeshList &Meshes() const;
  void RemoveMesh(HdAnariMesh *m);

  int SceneVersion() const;
  void MarkNewSceneVersion();

 private:
  anari::Device _device{nullptr};
  anari::Material _material{nullptr};

  std::mutex _mutex;
  MeshList _meshes;
  std::atomic<int> _sceneVersion{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline HdAnariRenderParam::HdAnariRenderParam(anari::Device d) : _device(d)
{
  if (!d)
    return;

  _material = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, _material, "alphaMode", "opaque");
  anari::setParameter(d, _material, "color", "color");
  anari::commitParameters(d, _material);
  // TODO: set some non-default material parameters to highlight use
}

inline HdAnariRenderParam::~HdAnariRenderParam()
{
  if (!_device)
    return;

  anari::release(_device, _material);
  anari::release(_device, _device);
}

inline anari::Device HdAnariRenderParam::GetANARIDevice() const
{
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
