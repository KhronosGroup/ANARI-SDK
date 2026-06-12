// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "material.h"

#include "debugCodes.h"
#include "material/matte.h"
#include "material/physicallyBased.h"
#include "material/textureLoader.h"

#ifdef HDANARI_ENABLE_MDL
#include "material/mdl.h"
#endif

#include "renderDelegate.h"
#include "renderParam.h"

#include <anari/anari.h>
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
// pxr
#include <anari/frontend/anari_enums.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/trace/staticKeyData.h>
#include <pxr/base/trace/trace.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/materialNetwork2Interface.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

// Helper functions ///////////////////////////////////////////////////////////

// HdAnariMaterial ////////////////////////////////////////////////////////////

HdMaterialNetwork2 HdAnariMaterial::convertToHdMaterialNetwork2(
    const HdMaterialNetworkMap &hdNetworkMap)
{
  // Rip-off from USD/HdSt code
  HD_TRACE_FUNCTION();
  HdMaterialNetwork2 result;

  for (auto const &iter : hdNetworkMap.map) {
    const TfToken &terminalName = iter.first;
    const HdMaterialNetwork &hdNetwork = iter.second;

    // Transfer over individual nodes.
    // Note that the same nodes may be shared by multiple terminals.
    // We simply overwrite them here.
    if (hdNetwork.nodes.empty()) {
      continue;
    }
    for (const HdMaterialNode &node : hdNetwork.nodes) {
      HdMaterialNode2 &materialNode2 = result.nodes[node.path];
      materialNode2.nodeTypeId = node.identifier;
      materialNode2.parameters = node.parameters;
    }
    // Assume that the last entry is the terminal
    result.terminals[terminalName].upstreamNode = hdNetwork.nodes.back().path;

    // Transfer relationships to inputConnections on receiving/downstream nodes.
    for (const HdMaterialRelationship &rel : hdNetwork.relationships) {
      // outputId (in hdMaterial terms) is the input of the receiving node
      auto iter = result.nodes.find(rel.outputId);

      // skip connection if the destination node doesn't exist
      if (iter == result.nodes.end()) {
        continue;
      }

      std::vector<HdMaterialConnection2> &conns =
          iter->second.inputConnections[rel.outputName];
      HdMaterialConnection2 conn{rel.inputId, rel.inputName};

      // skip connection if it already exists (it may be shared
      // between surface and displacement)
      if (std::find(conns.begin(), conns.end(), conn) == conns.end()) {
        conns.push_back(std::move(conn));
      }
    }

    // Transfer primvars:
    result.primvars = hdNetwork.primvars;
  }
  return result;
}

HdAnariMaterial::HdAnariMaterial(anari::Device d, const SdfPath &id)
    : HdMaterial(id), device_(d)
{}

HdAnariMaterial::~HdAnariMaterial()
{
  ReleaseSamplers(device_, samplers_);
  samplers_.clear();
  if (material_ && ownsMaterial_)
    anari::release(device_, material_);
}

void HdAnariMaterial::ReleaseSamplers(
    anari::Device device, const SamplerMapping &samplers)
{
  for (auto sampler : samplers)
    anari::release(device, sampler.second);
}

void HdAnariMaterial::Finalize(HdRenderParam *renderParam)
{
  // Drop our reference into the shared MDL material cache (the cache, not this
  // prim, owns the anari material).
  if (!mdlCacheKey_.empty()) {
    static_cast<HdAnariRenderParam *>(renderParam)
        ->ReleaseMdlMaterial(mdlCacheKey_);
    mdlCacheKey_.clear();
    material_ = nullptr;
  }
  HdMaterial::Finalize(renderParam);
}

void HdAnariMaterial::Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam,
    HdDirtyBits *dirtyBits)
{
  TF_DEBUG_MSG(HD_ANARI_RD_MATERIAL, "Sync material %s\n", GetId().GetText());

  auto hdAnariRenderParam = static_cast<HdAnariRenderParam *>(renderParam);

  if ((*dirtyBits & HdMaterial::DirtyResource)) {
    if (material_) {
      // Forcing rprims to have a dirty material id to re-evaluate
      // their material state as we don't know which rprims are bound to
      // this one. We can skip this invalidation the first time this
      // material is Sync'd since any affected Rprim should already be
      // marked with a dirty material id.
      HdChangeTracker &changeTracker =
          sceneDelegate->GetRenderIndex().GetChangeTracker();
      changeTracker.MarkAllRprimsDirty(HdChangeTracker::DirtyMaterialId);
    }

    // Release the material we previously held before rebuilding it: either one
    // we created, or a reference into the shared MDL material cache.
    if (!mdlCacheKey_.empty()) {
      hdAnariRenderParam->ReleaseMdlMaterial(mdlCacheKey_);
      mdlCacheKey_.clear();
    } else if (material_ && ownsMaterial_) {
      anari::release(device_, material_);
    }
    material_ = nullptr;
    ownsMaterial_ = false;

    //  Find material network and store it as HdMaterialNetwork2 for later use.
    VtValue networkMapResource = sceneDelegate->GetMaterialResource(GetId());
    HdMaterialNetworkMap networkMap =
        networkMapResource.GetWithDefault<HdMaterialNetworkMap>();

    materialNetwork2_ = convertToHdMaterialNetwork2(networkMap);
    auto materialNetworkIface =
        HdMaterialNetwork2Interface(GetId(), &materialNetwork2_);

    auto surfaceTerminalConnection = materialNetworkIface.GetTerminalConnection(
        HdMaterialTerminalTokens->surface);
    if (!surfaceTerminalConnection.first) {
      // A material network with no surface terminal is valid (e.g. physics-only
      // materials); fall back to the shared default without taking ownership.
      material_ = hdAnariRenderParam->GetDefaultMaterial();
      primvars_.clear();
      textures_.clear();
      *dirtyBits = HdChangeTracker::Clean;
      return;
    }

    // Try and guess the appropriate implementation for the surface terminal
    // type
    auto terminalType = materialNetworkIface.GetNodeType(
        surfaceTerminalConnection.second.upstreamNodeName);
    // Assume Matte is a safe fallback.
    materialType_ = MaterialType::Matte;
#ifdef HDANARI_ENABLE_MDL
    if (terminalType.GetString().find("<mdl>") != std::string::npos) {
      materialType_ = MaterialType::Mdl;
    } else
#endif
        if (terminalType == HdAnariMaterialTokens->UsdPreviewSurface) {
      materialType_ = hdAnariRenderParam->GetMaterialType();
    }

    switch (materialType_) {
    case MaterialType::Matte: {
      // Create support material, enumerate primvars and textures
      material_ =
          HdAnariMatteMaterial::CreateMaterial(device_, materialNetworkIface);
      primvars_ = HdAnariMatteMaterial::EnumeratePrimvars(
          materialNetworkIface, HdMaterialTerminalTokens->surface);
      textures_ = HdAnariMatteMaterial::EnumerateTextures(
          materialNetworkIface, HdMaterialTerminalTokens->surface);
      break;
    }
    case MaterialType::PhysicallyBased: {
      // Create support material, enumerate primvars and textures
      material_ = HdAnariPhysicallyBasedMaterial::CreateMaterial(
          device_, materialNetworkIface);
      primvars_ = HdAnariPhysicallyBasedMaterial::EnumeratePrimvars(
          materialNetworkIface, HdMaterialTerminalTokens->surface);
      textures_ = HdAnariPhysicallyBasedMaterial::EnumerateTextures(
          materialNetworkIface, HdMaterialTerminalTokens->surface);
      break;
    }
#ifdef HDANARI_ENABLE_MDL
    case MaterialType::Mdl: {
      // The anari material is created/shared via the render param cache in the
      // DirtyParams pass (it needs the resolved parameters to key on).
      primvars_ = HdAnariMdlMaterial::EnumeratePrimvars(
          materialNetworkIface, HdMaterialTerminalTokens->surface);
      textures_ = HdAnariMdlMaterial::EnumerateTextures(
          materialNetworkIface, HdMaterialTerminalTokens->surface);
      break;
    }
#endif
    }

    // Matte / physically based materials are created and owned here. MDL
    // materials are shared through the render param cache, not owned per prim.
    ownsMaterial_ = (materialType_ != MaterialType::Mdl);

    // Build primvar mapping so we can load the texture and configure the
    // related samplers
    attributes_ = BuildPrimvarBinding(primvars_);
  }

  // MDL materials are (re)acquired from the shared cache here. A DirtyResource
  // sync that arrives without DirtyParams has just cleared material_, so force
  // this pass in that case too — otherwise the material stays null until the
  // next params-dirty sync.
  const bool needsMdlAcquire =
      materialType_ == MaterialType::Mdl && material_ == nullptr;
  if ((*dirtyBits & HdMaterial::DirtyParams) || needsMdlAcquire) {
    auto materialNetworkIface =
        HdMaterialNetwork2Interface(GetId(), &materialNetwork2_);

    // Load the textures and create the samplers
    // FIXME: This one should be finer grain and only work on the diffs between
    // syncs. Load new textures and release unused ones...
    ReleaseSamplers(device_, samplers_);
    samplers_ = CreateSamplers(device_, textures_);

    switch (materialType_) {
    case MaterialType::Matte: {
      HdAnariMatteMaterial::SyncMaterialParameters(device_,
          material_,
          materialNetworkIface,
          attributes_,
          primvars_,
          samplers_);
      break;
    }
    case MaterialType::PhysicallyBased: {
      HdAnariPhysicallyBasedMaterial::SyncMaterialParameters(device_,
          material_,
          materialNetworkIface,
          attributes_,
          primvars_,
          samplers_);
      break;
    }
#ifdef HDANARI_ENABLE_MDL
    case MaterialType::Mdl: {
      // Share one anari material across instances with identical content so the
      // device compiles it and loads its textures only once.
      auto key = HdAnariMdlMaterial::ComputeContentKey(materialNetworkIface);
      if (key != mdlCacheKey_) {
        if (!mdlCacheKey_.empty())
          hdAnariRenderParam->ReleaseMdlMaterial(mdlCacheKey_);
        material_ = hdAnariRenderParam->AcquireMdlMaterial(key, [&]() {
          auto m =
              HdAnariMdlMaterial::CreateMaterial(device_, materialNetworkIface);
          HdAnariMdlMaterial::SyncMaterialParameters(device_,
              m,
              materialNetworkIface,
              attributes_,
              primvars_,
              samplers_);
          anari::commitParameters(device_, m);
          return m;
        });
        mdlCacheKey_ = key;
      }
      break;
    }
#endif
    }
  }

  // Commit the materials we own (matte / physically based). Shared MDL
  // materials are committed once when created in the cache; re-committing them
  // per instance would recompile and reset the renderer's accumulation.
  if ((*dirtyBits & (HdMaterial::DirtyResource | HdMaterial::DirtyParams))
      && material_ && ownsMaterial_)
    anari::commitParameters(device_, material_);

  // Clear every bit we were given, not just AllDirty: otherwise unhandled bits
  // stay set and Hydra re-Syncs this material on every frame.
  *dirtyBits = HdChangeTracker::Clean;
}

std::map<SdfPath, anari::Sampler> HdAnariMaterial::CreateSamplers(
    anari::Device device,
    const std::map<SdfPath, HdAnariTextureLoader::TextureDesc> &textureDescs)
{
  std::map<SdfPath, anari::Sampler> textures;
  for (auto &&[path, desc] : textureDescs) {
    TF_DEBUG_MSG(HD_ANARI_RD_MATERIAL,
        "Loading texture %s for %s\n",
        desc.assetPath.c_str(),
        path.GetText());

    auto array = HdAnariTextureLoader::LoadHioTexture2D(
        device, desc.assetPath, desc.minMagFilter, desc.colorspace);
    if (!array)
      continue;

    auto sampler = anari::newObject<anari::Sampler>(device, "image2D");
    anari::setAndReleaseParameter(device, sampler, "image", array);
    anari::setParameter(device,
        sampler,
        "filter",
        desc.minMagFilter == HdAnariTextureLoader::MinMagFilter::Nearest
            ? "nearest"
            : "linear");

    anari::setParameter(device,
        sampler,
        "outTransform",
        ANARI_FLOAT32_MAT4,
        desc.transform.data());

    anari::setParameter(
        device, sampler, "outOffset", ANARI_FLOAT32_VEC4, desc.offset.data());

    anari::commitParameters(device, sampler);

    textures[path] = sampler;
  }

  return textures;
}

HdAnariMaterial::PrimvarBinding HdAnariMaterial::BuildPrimvarBinding(
    const PrimvarMapping &primvarNames)
{
  PrimvarBinding primvarBinding;

  static const auto bindingNames = std::map<TfToken, TfToken>{
      {HdTokens->points, TfToken("position")},
      {HdTokens->normals, TfToken("normal")},
      {HdTokens->displayColor, TfToken("color")},
      {HdTokens->displayOpacity, TfToken("opacity")},
  };

  static const auto attributes = std::array{TfToken("attribute0"),
      TfToken("attribute1"),
      TfToken("attribute2"),
      TfToken("attribute3")};

  int nextIndex = 0;
  for (const auto &pvm : primvarNames) {
    auto primvarName = pvm.second;
    if (auto it = bindingNames.find(primvarName); it != cend(bindingNames)) {
      primvarBinding[primvarName] = it->second;
    } else {
      auto index = nextIndex++;
      if (index < attributes.size()) {
        primvarBinding[primvarName] = attributes[index];
      } else {
        TF_WARN("Too many custom primvars on material, skipping %s\n",
            primvarName.GetText());
        continue;
      }
    }
  }

  return primvarBinding;
}

HdDirtyBits HdAnariMaterial::GetInitialDirtyBitsMask() const
{
  return HdMaterial::DirtyBits::AllDirty;
}

anari::Material HdAnariMaterial::GetAnariMaterial() const
{
  return material_;
}

PXR_NAMESPACE_CLOSE_SCOPE
