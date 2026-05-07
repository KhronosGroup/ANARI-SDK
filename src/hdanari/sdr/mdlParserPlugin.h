// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/pxr.h>
#include <pxr/usd/sdr/declare.h>
#include <pxr/usd/sdr/parserPlugin.h>
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariMdlParserPlugin : public SdrParserPlugin
{
 public:
  SdrShaderNodeUniquePtr ParseShaderNode(
      const SdrShaderNodeDiscoveryResult &discoveryResult) override;

  const SdrTokenVec &GetDiscoveryTypes() const override;

  const TfToken &GetSourceType() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE
