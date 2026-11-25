// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DelegateRegistry.h"

#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/version.h>
#if HD_API_VERSION >= 98
#include <pxr/imaging/hd/rendererCreateArgsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#endif
#include <pxr/imaging/hf/pluginDesc.h>

#include <algorithm>
#include <cctype>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

std::string subtypeFromPluginId(const TfToken &pluginId)
{
  std::string s = pluginId.GetString();
  // Strip a leading "Hd" and a trailing "RendererPlugin", then lowercase.
  const std::string prefix = "Hd";
  const std::string suffix = "RendererPlugin";
  if (s.size() > prefix.size() && s.compare(0, prefix.size(), prefix) == 0)
    s.erase(0, prefix.size());
  if (s.size() > suffix.size()
      && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
    s.erase(s.size() - suffix.size());
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

// Documented exclusion: hdAnari is the *reverse* bridge — a Hydra delegate that
// drives ANARI devices. Exposing it here lets the forward device drive it, and
// hdAnari resolves its ANARI library through ANARI_LIBRARY; when that names
// this library, capability probing recurses (hydra → hdAnari → hydra → …),
// leaking one worker (thread, X connection, GL context) per cycle until the X
// server's client limit, then crashing. A feedback loop by construction, so it
// is off the surface entirely.
static bool excludedPlugin(const TfToken &pluginId)
{
  static const TfToken hdAnari("HdAnariRendererPlugin");
  return pluginId == hdAnari;
}

DelegateRegistry::DelegateRegistry()
{
  auto &reg = HdRendererPluginRegistry::GetInstance();

  HfPluginDescVector descs;
  reg.GetPluginDescs(&descs);
  descs.erase(std::remove_if(descs.begin(),
                  descs.end(),
                  [](const HfPluginDesc &d) { return excludedPlugin(d.id); }),
      descs.end());

  // The "default" alias always forwards to Storm when it is available; Hydra's
  // own default-delegate choice (registry priority order) is only the fallback,
  // since that order is what handed `default` to hdAnari in the first place.
  static const TfToken storm("HdStormRendererPlugin");
  TfToken defaultId;
  for (const HfPluginDesc &desc : descs) {
    if (desc.id == storm) {
      defaultId = desc.id;
      break;
    }
  }
  if (defaultId.IsEmpty()) {
#if HD_API_VERSION >= 98
    // OpenUSD 26.08 (HD_API_VERSION 103) removed the bool overload in favor
    // of one taking HdRendererCreateArgsSchema (available since 98).
    const HdRendererCreateArgsSchema createArgs(
        HdRendererCreateArgsSchema::Builder()
            .SetGpuEnabled(HdRetainedTypedSampledDataSource<bool>::New(true))
            .Build());
    defaultId = reg.GetDefaultPluginId(createArgs);
#else
    defaultId = reg.GetDefaultPluginId(/*gpuEnabled=*/true);
#endif
    if (excludedPlugin(defaultId))
      defaultId = descs.empty() ? TfToken() : descs.front().id;
  }

  {
    DelegateInfo d;
    d.subtype = "default";
    d.pluginId = defaultId;
    d.displayName = "Hydra default delegate";
    d.isDefault = true;
    m_delegates.push_back(std::move(d));
  }

  for (const HfPluginDesc &desc : descs) {
    DelegateInfo d;
    d.subtype = subtypeFromPluginId(desc.id);
    d.pluginId = desc.id;
    d.displayName = desc.displayName;
    d.isDefault = (desc.id == defaultId);
    m_delegates.push_back(std::move(d));
  }

  for (const auto &d : m_delegates)
    m_subtypeCStrs.push_back(d.subtype.c_str());
  m_subtypeCStrs.push_back(nullptr);
}

const DelegateRegistry &DelegateRegistry::get()
{
  static const DelegateRegistry s_instance;
  return s_instance;
}

TfToken DelegateRegistry::pluginForSubtype(const std::string &subtype) const
{
  for (const auto &d : m_delegates) {
    if (d.subtype == subtype)
      return d.pluginId;
  }
  return TfToken();
}

const char **DelegateRegistry::subtypeCStrings() const
{
  return const_cast<const char **>(m_subtypeCStrs.data());
}

} // namespace anari_hydra
