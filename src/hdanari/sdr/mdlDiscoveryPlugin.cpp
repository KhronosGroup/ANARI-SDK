// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mdlDiscoveryPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_DISCOVERY_PLUGIN(HdAnariMdlDiscoveryPlugin)

NdrNodeDiscoveryResultVec HdAnariMdlDiscoveryPlugin::DiscoverNodes(
    const Context &context)
{
  return NdrNodeDiscoveryResultVec{};
}

const NdrStringVec &HdAnariMdlDiscoveryPlugin::GetSearchURIs() const
{
  static NdrStringVec searchPaths = NdrStringVec{};
  return searchPaths;
}

PXR_NAMESPACE_CLOSE_SCOPE
