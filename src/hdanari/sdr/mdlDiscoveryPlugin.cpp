// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mdlDiscoveryPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

SDR_REGISTER_DISCOVERY_PLUGIN(HdAnariMdlDiscoveryPlugin)

SdrShaderNodeDiscoveryResultVec HdAnariMdlDiscoveryPlugin::DiscoverShaderNodes(
    const Context &context)
{
  return SdrShaderNodeDiscoveryResultVec{};
}

const SdrStringVec &HdAnariMdlDiscoveryPlugin::GetSearchURIs() const
{
  static SdrStringVec searchPaths = SdrStringVec{};
  return searchPaths;
}

PXR_NAMESPACE_CLOSE_SCOPE
