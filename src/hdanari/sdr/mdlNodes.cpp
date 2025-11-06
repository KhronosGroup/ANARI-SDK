// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mdlNodes.h"

#include "mdl/mdlRegistry.h"
#include "tokens.h"

#include <mi/base/handle.h>
#include <mi/neuraylib/annotation_wrapper.h>
#include <mi/neuraylib/iexpression.h>
#include <mi/neuraylib/imodule.h>
#include <mi/neuraylib/istring.h>
#include <mi/neuraylib/itexture.h>
#include <mi/neuraylib/itransaction.h>
#include <mi/neuraylib/itype.h>
#include <mi/neuraylib/ivalue.h>

#include <pxr/base/tf/scoped.h>
#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>
#include <pxr/usd/ndr/declare.h>
#include <pxr/usd/ndr/node.h>
#include <pxr/usd/ndr/property.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/declare.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <string>
#include <type_traits>

using namespace std::string_literals;
using namespace std::string_view_literals;

PXR_NAMESPACE_OPEN_SCOPE

MdlSdrShaderNode *MdlSdrShaderNode::ParseSdrDiscoveryResult(
    const NdrNodeDiscoveryResult &discoveryResult)
{
  auto registry = HdAnariMdlRegistry::GetInstance();
  if (!registry)
    return nullptr;

  if (discoveryResult.resolvedUri.length() > discoveryResult.uri.length()) {
    // Try and get a meaningful search path from the resolvedUri and uri
    // Somehow dumb logic but that does the trick enough to get us going...
    auto uri = discoveryResult.uri;
    auto resolvedUri = discoveryResult.resolvedUri;

    for (;;) {
      auto uriLastDash = uri.rfind('/');
      auto resolvedUriLastDash = resolvedUri.rfind('/');
      if (uriLastDash == std::string::npos
          || resolvedUriLastDash == std::string::npos) {
        break;
      }
      auto uriLast = uri.substr(uriLastDash);
      auto resolvedUriLast = resolvedUri.substr(resolvedUriLastDash);
      if (uriLast != resolvedUriLast) {
        break;
      }
      uri = uri.substr(0, uriLastDash);
      resolvedUri = resolvedUri.substr(0, resolvedUriLastDash);
    }

    registry->preprendUserSearchPath(resolvedUri);
  }

  auto transaction = mi::base::make_handle(registry->createTransaction());
  TfScoped releaseTransaction([&transaction]() { transaction->abort(); });

  auto module = mi::base::make_handle(
      registry->loadModule(discoveryResult.resolvedUri, transaction.get()));
  if (!module)
    return nullptr;

  TfScoped releaseModule([&module]() { module.reset(); });

  auto subIdentifier = discoveryResult.subIdentifier.GetString();
  if (subIdentifier.empty()) {
    if (auto len = discoveryResult.name.length(); len > 4) {
      auto extension = discoveryResult.name.substr(len - 4);
      if (extension == ".mdl") {
        subIdentifier = discoveryResult.name.substr(0, len - 4);
      }
    }
  }

  auto functionDefinition =
      mi::base::make_handle(registry->getFunctionDefinition(
          module.get(), subIdentifier, transaction.get()));
  TfScoped releaseFunctionDefinition(
      [&functionDefinition]() { functionDefinition.reset(); });

  if (functionDefinition->is_material()) {
    return new MdlMaterialSdrNode(
        discoveryResult, functionDefinition.get(), transaction.get());
  }

  return nullptr;
}

MdlFunctionSdrNode::MdlFunctionSdrNode(
    const NdrNodeDiscoveryResult &discoveryResult,
    const mi::neuraylib::IFunction_definition *functionDefinition,
    mi::neuraylib::ITransaction *transaction,
    NdrPropertyUniquePtrVec &&shaderProperties,
    NdrTokenMap &&shaderMetadata)
    : MdlSdrShaderNode(discoveryResult.identifier,
          discoveryResult.version,
          discoveryResult.name,
          discoveryResult.family,
          {},
          discoveryResult.sourceType,
          discoveryResult.uri,
          functionDefinition->get_mdl_name(),
          std::move(shaderProperties),
          std::move(shaderMetadata))
{}

MdlFunctionSdrNode::MdlFunctionSdrNode(
    const NdrNodeDiscoveryResult &discoveryResult,
    const mi::neuraylib::IFunction_definition *functionDefinition,
    mi::neuraylib::ITransaction *transaction)
    : MdlFunctionSdrNode(discoveryResult,
          functionDefinition,
          transaction,
          GetShaderProperties(discoveryResult, functionDefinition, transaction),
          GetShaderMetadata(discoveryResult, functionDefinition, transaction))
{}

NdrTokenMap MdlFunctionSdrNode::createMetadataFromAnnotation(
    const mi::neuraylib::Annotation_wrapper *annotations)
{
  NdrTokenMap metadata;

  if (auto index =
          annotations->get_annotation_index("::anno::in_group(string,bool)");
      index != mi::Size(-1)) {
    const char *group = {};
    annotations->get_annotation_param_value(index, 0, group);
    metadata[SdrPropertyMetadata->Page] = std::string(group);
  } else if (auto index = annotations->get_annotation_index(
                 "::anno::in_group(string,string,bool)");
      index != mi::Size(-1)) {
    const char *group = {};
    annotations->get_annotation_param_value(index, 0, group);
    auto page = std::string(group);
    annotations->get_annotation_param_value(index, 1, group);
    page += SdrPropertyTokens->PageDelimiter.GetString() + group;
    metadata[SdrPropertyMetadata->Page] = page;
  } else if (auto index = annotations->get_annotation_index(
                 "::anno::in_group(string,string,string,bool)");
      index != mi::Size(-1)) {
    const char *group = {};
    annotations->get_annotation_param_value(index, 0, group);
    auto page = std::string(group);
    annotations->get_annotation_param_value(index, 1, group);
    page += SdrPropertyTokens->PageDelimiter.GetString() + group;
    annotations->get_annotation_param_value(index, 2, group);
    page += SdrPropertyTokens->PageDelimiter.GetString() + group;
    metadata[SdrPropertyMetadata->Page] = page;
  }

  if (auto index =
          annotations->get_annotation_index("::anno::description(string)");
      index != mi::Size(-1)) {
    const char *description;
    annotations->get_annotation_param_value(index, 0, description);
    metadata[SdrPropertyMetadata->Help] = std::string(description);
  }

  if (auto index =
          annotations->get_annotation_index("::anno::display_name(string)");
      index != mi::Size(-1)) {
    const char *displayName = {};
    annotations->get_annotation_param_value(index, 0, displayName);
    metadata[SdrPropertyMetadata->Label] = std::string(displayName);
  }

  return metadata;
}

template <typename ValueType,
    typename TypeType = std::decay_t<decltype(ValueType::get_type())>>
SdrShaderPropertyUniquePtr MdlFunctionSdrNode::createScalarInputProperty(
    const std::string &name,
    const TypeType *scalarType,
    const mi::neuraylib::IExpression_constant *defaultValueExp,
    const mi::neuraylib::Annotation_wrapper *annotations)
{
  VtValue defaultValue;
  auto scalarValue =
      mi::base::make_handle(defaultValueExp->get_value<ValueType>());
  if (scalarValue.is_valid_interface()) {
    defaultValue = scalarValue->get_value();
  }

  auto metadata = createMetadataFromAnnotation(annotations);

  using ScalarType = std::decay_t<decltype(scalarValue->get_value())>;
  TfToken renderType;
  TfToken propertyType;
  if constexpr (std::is_same_v<ScalarType, bool>) {
    renderType = HdAnariSdrTokens->bool1;
    propertyType = HdAnariSdrTokens->bool1;
  } else if constexpr (std::is_same_v<ScalarType, float>) {
    renderType = HdAnariSdrTokens->float1;
    propertyType = HdAnariSdrTokens->float1;
  } else if constexpr (std::is_same_v<ScalarType, int>) {
    renderType = HdAnariSdrTokens->int1;
    propertyType = HdAnariSdrTokens->int1;
  } else if constexpr (std::is_same_v<ScalarType, char *>) {
    renderType = HdAnariSdrTokens->string;
    propertyType = HdAnariSdrTokens->string;
  }

  metadata.insert({SdrPropertyMetadata->RenderType, renderType});

  return SdrShaderPropertyUniquePtr(new SdrShaderProperty(TfToken(name),
      propertyType, // type
      defaultValue, // default value
      false, // isOutput
      0, // arraySize
      metadata, // metadata
      {}, // hints
      {} // options
      ));
}

SdrShaderPropertyUniquePtr MdlFunctionSdrNode::createTextureInputProperty(
    const std::string &name,
    const mi::neuraylib::IType_texture *textureType,
    const mi::neuraylib::IExpression_constant *defaultValueExp,
    const mi::neuraylib::Annotation_wrapper *annotations)
{
  VtValue defaultValue;
  auto textureValue = mi::base::make_handle(
      defaultValueExp->get_value<const mi::neuraylib::IValue_texture>());
  if (textureValue.is_valid_interface()) {
    if (auto path = textureValue->get_value())
      defaultValue = SdfAssetPath(path);
  }

  auto metadata = createMetadataFromAnnotation(annotations);

  TfToken renderType;
  auto shape = textureType->get_shape();
  switch (shape) {
  case mi::neuraylib::IType_texture::Shape::TS_2D:
    renderType = HdAnariSdrTokens->texture2d;
    break;
  case mi::neuraylib::IType_texture::Shape::TS_3D:
    renderType = HdAnariSdrTokens->texture3d;
    break;
  default:
    break;
  }

  metadata.insert({SdrPropertyMetadata->RenderType, renderType});

  return SdrShaderPropertyUniquePtr(new SdrShaderProperty(TfToken(name),
      SdrPropertyTypes->Vstruct, // type
      defaultValue, // default value
      false, // isOutput
      0, // arraySize
      metadata, // metadata
      {}, // hints
      {} // options
      ));
}

NdrPropertyUniquePtrVec MdlFunctionSdrNode::GetShaderProperties(
    const NdrNodeDiscoveryResult &discoveryResult,
    const mi::neuraylib::IFunction_definition *functionDefinition,
    mi::neuraylib::ITransaction *transaction)
{
  NdrPropertyUniquePtrVec properties;

  return properties;

  // handle return type
  auto returnType =
      mi::base::make_handle(functionDefinition->get_return_type());

  // Parameters
  auto count = functionDefinition->get_parameter_count();

  auto types = mi::base::make_handle(functionDefinition->get_parameter_types());
  auto defaults = mi::base::make_handle(functionDefinition->get_defaults());
  auto annotationList =
      mi::base::make_handle(functionDefinition->get_parameter_annotations());

  for (mi::Size index = 0; index < count; index++) {
    auto type =
        mi::base::make_handle(types->get_type(index)->skip_all_type_aliases());
    std::string name = functionDefinition->get_parameter_name(index);
    auto defaultValue =
        mi::base::make_handle(defaults->get_expression(name.c_str()));
    auto annotationBlock =
        mi::base::make_handle(annotationList->get_annotation_block(index));
    auto annotations = mi::neuraylib::Annotation_wrapper(annotationBlock.get());

    mi::neuraylib::Annotation_wrapper annotationWrapper(annotations);

    switch (type->get_kind()) {
    // case mi::neuraylib::IType::TK_STRING: {
    //     properties.push_back(
    //         createScalarInputProperty<mi::neuraylib::IValue_string>(name,
    //             type->get_interface<const mi::neuraylib::IType_string>(),
    //             defaultValue->get_interface<const
    //             mi::neuraylib::IExpression_constant>(), &annotationWrapper
    //     ));
    //     break;
    // }
    case mi::neuraylib::IType::TK_TEXTURE: {
      properties.push_back(createTextureInputProperty(name,
          type->get_interface<const mi::neuraylib::IType_texture>(),
          defaultValue
              ->get_interface<const mi::neuraylib::IExpression_constant>(),
          &annotationWrapper));
      break;
    }
    case mi::neuraylib::IType::TK_BOOL: {
      properties.push_back(
          createScalarInputProperty<mi::neuraylib::IValue_bool>(name,
              type->get_interface<const mi::neuraylib::IType_bool>(),
              defaultValue
                  ->get_interface<const mi::neuraylib::IExpression_constant>(),
              &annotationWrapper));
      break;
    }
    case mi::neuraylib::IType::TK_INT: {
      properties.push_back(
          createScalarInputProperty<mi::neuraylib::IValue_int>(name,
              type->get_interface<const mi::neuraylib::IType_int>(),
              defaultValue
                  ->get_interface<const mi::neuraylib::IExpression_constant>(),
              &annotationWrapper));
      break;
    }
    case mi::neuraylib::IType::TK_FLOAT: {
      properties.push_back(
          createScalarInputProperty<mi::neuraylib::IValue_float>(name,
              type->get_interface<const mi::neuraylib::IType_float>(),
              defaultValue
                  ->get_interface<const mi::neuraylib::IExpression_constant>(),
              &annotationWrapper));
      break;
    }
    default: {
      break;
    }
    }
  }

  return properties;
}

NdrTokenMap MdlFunctionSdrNode::GetShaderMetadata(
    const NdrNodeDiscoveryResult &discoveryResult,
    const mi::neuraylib::IFunction_definition *functionDefinition,
    mi::neuraylib::ITransaction *transaction)
{
  auto metadata = discoveryResult.metadata;
  metadata[TfToken("version")] = "1"s;
  metadata[TfToken("uri")] = functionDefinition->get_mdl_module_name();
  metadata[TfToken("subIdentifier")] =
      functionDefinition->get_mdl_simple_name();
  return metadata;
}

MdlMaterialSdrNode::MdlMaterialSdrNode(
    const NdrNodeDiscoveryResult &discoveryResult,
    const mi::neuraylib::IFunction_definition *functionDefinition,
    mi::neuraylib::ITransaction *transaction)
    : MdlFunctionSdrNode(discoveryResult,
          functionDefinition,
          transaction,
          GetShaderProperties(discoveryResult, functionDefinition, transaction),
          GetShaderMetadata(discoveryResult, functionDefinition, transaction))
{}

NdrPropertyUniquePtrVec MdlMaterialSdrNode::GetShaderProperties(
    const NdrNodeDiscoveryResult &discoveryResult,
    const mi::neuraylib::IFunction_definition *functionDefinition,
    mi::neuraylib::ITransaction *transaction)
{
  auto properties = MdlFunctionSdrNode::GetShaderProperties(
      discoveryResult, functionDefinition, transaction);
  properties.emplace_back(new SdrShaderProperty(TfToken("out"),
      SdrPropertyTypes->Terminal, // type
      {}, // default value
      true, // isOutput
      0, // arraySize
      {
          // Terminal //Struct
          {SdrPropertyMetadata->RenderType, "terminal surface"s},
          {TfToken("structType"), "material"s},
          {TfToken("symbol"), "material"s},
      }, // metadata
      {}, // hints
      {} // options
      ));

  return properties;
}

NdrTokenMap MdlMaterialSdrNode::GetShaderMetadata(
    const NdrNodeDiscoveryResult &discoveryResult,
    const mi::neuraylib::IFunction_definition *functionDefinition,
    mi::neuraylib::ITransaction *transaction)
{
  return MdlFunctionSdrNode::GetShaderMetadata(
      discoveryResult, functionDefinition, transaction);
}

PXR_NAMESPACE_CLOSE_SCOPE
