// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mdlParserPlugin.h"
#include "mdlNodes.h"
#include "tokens.h"

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/pxr.h>
#include <pxr/usd/ndr/declare.h>
#include <pxr/usd/ndr/node.h>
#include <pxr/usd/sdr/declare.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

using namespace std::string_literals;

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(HdAnariMdlParserPlugin)

NdrNodeUniquePtr HdAnariMdlParserPlugin::Parse(
    const NdrNodeDiscoveryResult &discoveryRes)
{
  auto sdrNode = MdlSdrShaderNode::ParseSdrDiscoveryResult(discoveryRes);
  return NdrNodeUniquePtr(sdrNode);
}

const NdrTokenVec &HdAnariMdlParserPlugin::GetDiscoveryTypes() const
{
  static const NdrTokenVec discoveryTypes = {HdAnariSdrTokens->mdl};
  return discoveryTypes;
}

const TfToken &HdAnariMdlParserPlugin::GetSourceType() const
{
  return HdAnariSdrTokens->mdl;
}

PXR_NAMESPACE_CLOSE_SCOPE
