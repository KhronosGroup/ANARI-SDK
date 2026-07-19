// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/pxr.h>
#include <pxr/usd/sdr/declare.h>
#include <pxr/usd/sdr/discoveryPlugin.h>
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariMdlDiscoveryPlugin final : public SdrDiscoveryPlugin
{
 public:
  HdAnariMdlDiscoveryPlugin() = default;

  ~HdAnariMdlDiscoveryPlugin() = default;

  /// Discover all of the nodes that appear within the the search paths
  /// provided and match the extensions provided.
  SdrShaderNodeDiscoveryResultVec DiscoverShaderNodes(
      const Context &) override;

  /// Gets the paths that this plugin is searching for nodes in.
  const SdrStringVec &GetSearchURIs() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE
