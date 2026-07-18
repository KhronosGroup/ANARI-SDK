// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "renderSettings.h"
#include "renderDelegate.h"

#include <cstring>

#include <pxr/base/tf/stringUtils.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderSettingsSchema.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/sceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {
// Render settings authored on a UsdRenderSettings prim are surfaced by USD only
// when namespaced; hdAnari collects the "anari:" ones (see
// GetRenderSettingsNamespaces) and strips the prefix to the plain setting token
// (`anari:renderSubtype` -> `renderSubtype`).
constexpr const char *kNamespacePrefix = "anari:";
} // namespace

HdAnariRenderSettings::HdAnariRenderSettings(SdfPath const &id)
    : HdRenderSettings(id)
{}

HdAnariRenderSettings::~HdAnariRenderSettings() = default;

void HdAnariRenderSettings::_Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam,
    const HdDirtyBits *dirtyBits)
{
  // The legacy scene-delegate path leaves the base class's namespaced settings
  // empty, so read them from the terminal scene index directly and route them
  // into the delegate's setting map that the render pass reads via
  // GetRenderSetting().
  auto &renderIndex = sceneDelegate->GetRenderIndex();
  auto si = renderIndex.GetTerminalSceneIndex();
  if (!si)
    return;

  const HdSceneIndexPrim prim = si->GetPrim(GetId());
  HdRenderSettingsSchema rs =
      HdRenderSettingsSchema::GetFromParent(prim.dataSource);
  if (!rs)
    return;

  const HdContainerDataSourceHandle settings =
      rs.GetNamespacedSettings().GetContainer();
  if (!settings)
    return;

  auto *renderDelegate =
      static_cast<HdAnariRenderDelegate *>(renderIndex.GetRenderDelegate());

  for (const TfToken &name : settings->GetNames()) {
    if (!TfStringStartsWith(name.GetString(), kNamespacePrefix))
      continue;
    auto ds = HdSampledDataSource::Cast(settings->Get(name));
    if (!ds)
      continue;
    const std::string key =
        name.GetString().substr(std::strlen(kNamespacePrefix));
    renderDelegate->SetRenderSetting(TfToken(key), ds->GetValue(0.0f));
  }
}

PXR_NAMESPACE_CLOSE_SCOPE
