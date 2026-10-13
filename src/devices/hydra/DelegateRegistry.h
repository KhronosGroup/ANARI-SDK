// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Enumerates the Hydra renderer-plugin registry and exposes each delegate as an
// ANARI device subtype (SPEC §11.2). Tokens are de-prefixed and lowercased
// ("HdStormRendererPlugin" -> "storm"); a "default" alias forwards to Storm
// when available, else Hydra's own default-delegate choice. One documented
// exclusion: hdAnari (the reverse bridge) is off the surface — driving it from
// the forward device is a feedback loop (see DelegateRegistry.cpp).

#pragma once

#include <pxr/base/tf/token.h>

#include <string>
#include <vector>

namespace anari_hydra {

struct DelegateInfo
{
  std::string subtype;          // ANARI device subtype, e.g. "storm"
  pxr::TfToken pluginId;        // Hydra plugin id, e.g. "HdStormRendererPlugin"
  std::string displayName;      // human-readable
  bool isDefault{false};        // matches Hydra's default-delegate choice
};

// The delegate registry is a process-global snapshot of the Hydra plugin
// registry, computed lazily on first use.
struct DelegateRegistry
{
  static const DelegateRegistry &get();

  // All delegates, plus the synthetic "default" alias at the front.
  const std::vector<DelegateInfo> &delegates() const { return m_delegates; }

  // Resolve an ANARI device subtype ("storm", "default", ...) to its plugin id.
  // Returns an empty token if the subtype is unknown.
  pxr::TfToken pluginForSubtype(const std::string &subtype) const;

  // Null-terminated array of subtype strings for anariGetDeviceSubtypes().
  const char **subtypeCStrings() const;

 private:
  DelegateRegistry();

  std::vector<DelegateInfo> m_delegates;
  std::vector<const char *> m_subtypeCStrs;   // stable storage, null-terminated
};

// Convert a Hydra plugin id token to an ANARI device subtype string.
std::string subtypeFromPluginId(const pxr::TfToken &pluginId);

} // namespace anari_hydra
