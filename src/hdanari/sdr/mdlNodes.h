// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mi/neuraylib/annotation_wrapper.h>
#include <mi/neuraylib/ifunction_definition.h>
#include <mi/neuraylib/itransaction.h>

#include <pxr/pxr.h>
#include <pxr/usd/sdr/declare.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>

PXR_NAMESPACE_OPEN_SCOPE

class MdlSdrShaderNode : public SdrShaderNode
{
 public:
  using SdrShaderNode::SdrShaderNode;

  static MdlSdrShaderNode *ParseSdrDiscoveryResult(
      const SdrShaderNodeDiscoveryResult &discoveryResult);
};

class MdlFunctionSdrNode : public MdlSdrShaderNode
{
  friend class MdlSdrShaderNode;

 protected:
  MdlFunctionSdrNode(const SdrShaderNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction,
      SdrShaderPropertyUniquePtrVec &&shaderProperties,
      SdrTokenMap &&shaderMetadata);

  static SdrShaderPropertyUniquePtrVec GetShaderProperties(
      const SdrShaderNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);
  static SdrTokenMap GetShaderMetadata(
      const SdrShaderNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);

  static SdrTokenMap createMetadataFromAnnotation(
      const mi::neuraylib::Annotation_wrapper *annotations);

  template <typename ScalarType, typename ITypeType>
  static SdrShaderPropertyUniquePtr createScalarInputProperty(
      const std::string &name,
      const ITypeType *textureType,
      const mi::neuraylib::IExpression_constant *defaultValueExp,
      const mi::neuraylib::Annotation_wrapper *annotations);
  static SdrShaderPropertyUniquePtr createStringInputProperty(
      const std::string &name,
      const mi::neuraylib::IType_string *scalarType,
      const mi::neuraylib::IExpression_constant *defaultValueExp,
      const mi::neuraylib::Annotation_wrapper *annotations);
  static SdrShaderPropertyUniquePtr createTextureInputProperty(
      const std::string &name,
      const mi::neuraylib::IType_texture *textureType,
      const mi::neuraylib::IExpression_constant *defaultValueExp,
      const mi::neuraylib::Annotation_wrapper *annotations);

 private:
  MdlFunctionSdrNode(const SdrShaderNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);
};

class MdlMaterialSdrNode : public MdlFunctionSdrNode
{
  friend class MdlSdrShaderNode;

 protected:
  static SdrShaderPropertyUniquePtrVec GetShaderProperties(
      const SdrShaderNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);
  static SdrTokenMap GetShaderMetadata(
      const SdrShaderNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);

 private:
  MdlMaterialSdrNode(const SdrShaderNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);
};

PXR_NAMESPACE_CLOSE_SCOPE
