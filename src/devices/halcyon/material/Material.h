// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "renderer/pathtracer/MaterialConverter.h"
#include "sampler/Sampler.h"
// std
#include <memory>
#include <optional>
#include <vector>

namespace halcyon {

namespace pathtracer {
class CppMaterial;
} // namespace pathtracer

// Inherit from this, add your functionality, and add it to 'createInstance()'.
// Subtypes read their color parameter in commitParameters(); finalize()
// mirrors the whole state into ISPC and compiles the subtype's MaterialXCpp
// graph (buildGraph()) into the C++ material the pathtracer shades.
struct Material : public Object
{
  Material(HalcyonDeviceGlobalState *s);
  ~Material() override;
  static Material *createInstance(
      std::string_view subtype, HalcyonDeviceGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  // Whether the material's validity can change with its parameters (a
  // 'materialx' document that doesn't read is invalid): surfaces then
  // observe it, so they pick up a change of validity. Other subtypes are
  // always valid.
  virtual bool validityCanChange() const;

 protected:
  // Reads color parameter 'name': a sampler, a float3/float4 constant
  // ('defaultColor' if unset) or an attribute name ("none" for the constant)
  void commitColorParameter(
      const char *name, const float4 &defaultColor = float4(1.f));
  // Reads opacity parameter 'name' likewise ('opacity' unless a subtype
  // reads another), 1 if unset
  void commitOpacityParameter(const char *name);
  // Whether the opacity is the constant 1 (unset, or an unknown attribute)
  bool opacityIsOne() const;

  // Where parameter 'name' comes from, for the pathtracer's graph: a sampler,
  // an attribute name, or a float/float3/float4 constant ('defaultValue' when
  // unset or an unknown attribute name, as the other renderers read it).
  // Keeps the sampler alive until the next commit, and finalizes the material
  // again whenever the sampler is (its graph reads the sampler's inAttribute).
  pathtracer::MaterialParameter readGraphParameter(
      const char *name, const mxcpp::Vec4f &defaultValue);

  // readGraphParameter() of every input of 'inputs' into 'values', each the
  // parameter of its name with the input's default
  void readShaderInputs(const std::vector<pathtracer::ShaderInput> &inputs,
      std::vector<pathtracer::MaterialParameter> &values);
  // A samplers-only parameter's sampler (its ISPC object), kept alive like
  // readGraphParameter()'s; NULL if unset
  const void *samplerOf(const char *name);
  // readGraphParameter() if parameter 'name' is set, else nothing
  std::optional<pathtracer::MaterialParameter> readOptionalGraphParameter(
      const char *name, const mxcpp::Vec4f &defaultValue);
  // readOptionalGraphParameter() for an input whose default is part of the
  // graph (a normal, a tangent): nothing where the parameter names no
  // attribute either; dropInvalidSampler() then drops an invalid sampler
  std::optional<pathtracer::MaterialParameter> readGraphInputParameter(
      const char *name);
  // Whether the sampler 'ie' (a readGraphParameter() or samplerOf() result)
  // is valid now; buildGraph() runs after every sampler change, so a graph
  // that asks there follows it
  bool graphSamplerIsValid(const void *ie) const;
  // 'p' left unset where it is an invalid sampler
  void dropInvalidSampler(std::optional<pathtracer::MaterialParameter> &p) const;

  // The pathtracer's graph of the subtype as committed, with the samplers it
  // names; false if it has none (the pathtracer then lights it as a surface
  // without a material). Lists in 'unsupported' the sampler parameters the
  // graph doesn't use.
  virtual bool buildGraph(pathtracer::ConvertedMaterial &graph,
      std::vector<std::string> &unsupported) const;
  // How the pathtracer shades the subtype once its graph compiled, an
  // ispc::MaterialKind (an int: this header doesn't see ispc's enums): the
  // graph in C++ (MATERIAL_KIND_CPP, the default), or a native kind of
  // PathMaterial.isph validated against the graph, which the renderer's
  // 'nativeMaterials' false shades instead
  virtual int nativeKind() const;
  // Whether the colour parameter 'colorName' and 'opacity' have types the
  // colour and opacity readers native kinds share with the other renderers
  // read as the graph does: not a FLOAT32 colour, nor a FLOAT32_VEC3/VEC4
  // opacity, which the specification doesn't allow and only the graph reads
  bool nativeColorReadsAgree(const char *colorName) const;
  // Reports that the subtype's graph doesn't compile ('diagnostic' says
  // why); a WARNING by default, as the pathtracer then lights the surface as
  // one without a material
  virtual void reportGraphFailure(const std::string &diagnostic);
  // Whether finalize() compiled the subtype's graph
  bool hasCompiledGraph() const;
  // MATERIAL_KIND_PHYSICALLY_BASED's parameters (an
  // ispc::PhysicallyBasedParameters the subtype holds), which finalize()
  // hands to ISPC while that is the native kind; NULL by default
  virtual const void *physicallyBasedParameters() const;

  float4 m_color{1.f, 1.f, 1.f, 1.f};
  Attribute m_colorAttribute{Attribute::NONE};
  helium::IntrusivePtr<Sampler> m_colorSampler;

  float m_opacity{1.f};
  Attribute m_opacityAttribute{Attribute::NONE};
  helium::IntrusivePtr<Sampler> m_opacitySampler;

  AlphaMode m_alphaMode{AlphaMode::OPAQUE};
  float m_alphaCutoff{0.5f};
  // Whether the colour's alpha multiplies the opacity in the other renderers
  // (false where the subtype's colour is a color3)
  bool m_colorAlphaIsOpacity{true};

 private:
  // The samplers readGraphParameter() returned, observed. Only ever cleared
  // as a whole: helium's removeChangeObserver() drops every registration of
  // this material on a sampler, so removing one of two entries for the same
  // sampler would stop the other's notifications.
  std::vector<helium::ChangeObserverPtr<Sampler>> m_graphSamplers;
  // What the pathtracer shades (ISPC Material::cppMaterial), replaced only
  // in finalize()
  std::unique_ptr<pathtracer::CppMaterial> m_cppMaterial;
};

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Material *, ANARI_MATERIAL);
