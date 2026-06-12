// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#ifndef HDANARI_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H
#define HDANARI_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H

#include <pxr/imaging/hd/sceneIndexPlugin.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Scene index plugin that turns USD implicit surfaces (sphere, cube, cone,
/// cylinder, capsule, plane) into meshes. HdAnari only supports the `mesh` and
/// `points` rprim types, so without this conversion implicit prims are silently
/// dropped by the render index.
class HdAnari_ImplicitSurfaceSceneIndexPlugin : public HdSceneIndexPlugin
{
 public:
  HdAnari_ImplicitSurfaceSceneIndexPlugin();

 protected:
  HdSceneIndexBaseRefPtr _AppendSceneIndex(
      const HdSceneIndexBaseRefPtr &inputScene,
      const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDANARI_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H
