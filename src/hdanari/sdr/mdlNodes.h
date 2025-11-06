// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mi/neuraylib/annotation_wrapper.h>
#include <mi/neuraylib/ifunction_definition.h>
#include <mi/neuraylib/itransaction.h>

#include <pxr/pxr.h>
#include <pxr/usd/ndr/declare.h>
#include <pxr/usd/sdr/shaderNode.h>

PXR_NAMESPACE_OPEN_SCOPE

class MdlSdrShaderNode : public SdrShaderNode
{
 public:
  using SdrShaderNode::SdrShaderNode;

  static MdlSdrShaderNode *ParseSdrDiscoveryResult(
      const NdrNodeDiscoveryResult &discoveryResult);
};

class MdlFunctionSdrNode : public MdlSdrShaderNode
{
  friend class MdlSdrShaderNode;

 protected:
  MdlFunctionSdrNode(const NdrNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction,
      NdrPropertyUniquePtrVec &&shaderProperties,
      NdrTokenMap &&shaderMetadata);

  static NdrPropertyUniquePtrVec GetShaderProperties(
      const NdrNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);
  static NdrTokenMap GetShaderMetadata(
      const NdrNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);

  static NdrTokenMap createMetadataFromAnnotation(
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
  MdlFunctionSdrNode(const NdrNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);
};

class MdlMaterialSdrNode : public MdlFunctionSdrNode
{
  friend class MdlSdrShaderNode;

 protected:
  static NdrPropertyUniquePtrVec GetShaderProperties(
      const NdrNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);
  static NdrTokenMap GetShaderMetadata(
      const NdrNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);

 private:
  MdlMaterialSdrNode(const NdrNodeDiscoveryResult &discoveryResult,
      const mi::neuraylib::IFunction_definition *functionDefinition,
      mi::neuraylib::ITransaction *transaction);
};

PXR_NAMESPACE_CLOSE_SCOPE
