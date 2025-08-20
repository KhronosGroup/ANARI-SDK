// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/pxr.h>
#include <pxr/usd/ndr/discoveryPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariMdlDiscoveryPlugin final : public NdrDiscoveryPlugin
{
 public:
  HdAnariMdlDiscoveryPlugin() = default;

  ~HdAnariMdlDiscoveryPlugin() = default;

  /// Discover all of the nodes that appear within the the search paths
  /// provided and match the extensions provided.
  NdrNodeDiscoveryResultVec DiscoverNodes(const Context &) override;

  /// Gets the paths that this plugin is searching for nodes in.
  const NdrStringVec &GetSearchURIs() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE
