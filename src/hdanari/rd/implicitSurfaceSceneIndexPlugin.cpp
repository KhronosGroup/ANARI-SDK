// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "implicitSurfaceSceneIndexPlugin.h"

#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdsi/implicitSurfaceSceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((sceneIndexPluginName, "HdAnari_ImplicitSurfaceSceneIndexPlugin")));

// Must match the renderer plugin displayName in plugInfo.json.in.
static const char *const _pluginDisplayName = "Anari";

TF_REGISTRY_FUNCTION(TfType)
{
  HdSceneIndexPluginRegistry::Define<HdAnari_ImplicitSurfaceSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
  const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

  HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
      _pluginDisplayName,
      _tokens->sceneIndexPluginName,
      /* inputArgs = */ nullptr,
      insertionPhase,
      HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdAnari_ImplicitSurfaceSceneIndexPlugin::
    HdAnari_ImplicitSurfaceSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HdAnari_ImplicitSurfaceSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
  // HdAnari has no native implicit geometry, so emit meshes for every type.
  HdDataSourceBaseHandle const toMeshSrc =
      HdRetainedTypedSampledDataSource<TfToken>::New(
          HdsiImplicitSurfaceSceneIndexTokens->toMesh);

  HdContainerDataSourceHandle const localInputArgs =
      HdRetainedContainerDataSource::New(HdPrimTypeTokens->sphere,
          toMeshSrc,
          HdPrimTypeTokens->cube,
          toMeshSrc,
          HdPrimTypeTokens->cone,
          toMeshSrc,
          HdPrimTypeTokens->cylinder,
          toMeshSrc,
          HdPrimTypeTokens->capsule,
          toMeshSrc,
          HdPrimTypeTokens->plane,
          toMeshSrc);

  return HdsiImplicitSurfaceSceneIndex::New(inputScene, localInputArgs);
}

PXR_NAMESPACE_CLOSE_SCOPE
