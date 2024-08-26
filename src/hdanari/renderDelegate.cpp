// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include <anari/anari.h>
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
#include <anari/frontend/anari_enums.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/sprim.h>
#include <pxr/imaging/hd/types.h>
#include <stdio.h>
#include <cstdlib>
#include <string>

#include "debugCodes.h"

#define HDANARI_TYPE_DEFINITIONS
// XXX: Add other Sprim types later
#include <pxr/imaging/hd/bprim.h>
// XXX: Add other Rprim types later
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/extComputation.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/tokens.h>

#include "instancer.h"
#include "material/matte.h"
#include "material/physicallyBased.h"
#include "mesh.h"
#include "renderBuffer.h"
#include "renderDelegate.h"
#include "renderPass.h"
// XXX: Add bprim types

static void hdAnariDeviceStatusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR)
    fprintf(stderr, "[ANARI][FATAL] %s\n", message);
  else if (severity == ANARI_SEVERITY_ERROR)
    fprintf(stderr, "[ANARI][ERROR] %s\n", message);
  else if (severity == ANARI_SEVERITY_WARNING)
    fprintf(stderr, "[ANARI][WARN ] %s\n", message);
  else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING)
    fprintf(stderr, "[ANARI][PERF ] %s\n", message);
  else if (severity == ANARI_SEVERITY_INFO)
    fprintf(stderr, "[ANARI][INFO ] %s\n", message);
#if 0
  else if (severity == ANARI_SEVERITY_DEBUG)
    fprintf(stderr, "[ANARI][DEBUG] %s\n", message);
#endif
}

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(
    HdAnariRenderSettingsTokens, HDANARI_RENDER_SETTINGS_TOKENS);

const TfTokenVector HdAnariRenderDelegate::SUPPORTED_RPRIM_TYPES = {
    HdPrimTypeTokens->mesh,
};

const TfTokenVector HdAnariRenderDelegate::SUPPORTED_SPRIM_TYPES = {
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->material,
    HdPrimTypeTokens->extComputation,
};

const TfTokenVector HdAnariRenderDelegate::SUPPORTED_BPRIM_TYPES = {
    HdPrimTypeTokens->renderBuffer,
};

std::mutex HdAnariRenderDelegate::_mutexResourceRegistry;
std::atomic_int HdAnariRenderDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr HdAnariRenderDelegate::_resourceRegistry;

HdAnariRenderDelegate::HdAnariRenderDelegate() : HdRenderDelegate()
{
  Initialize();
}

HdAnariRenderDelegate::HdAnariRenderDelegate(
    HdRenderSettingsMap const &settingsMap)
    : HdRenderDelegate(settingsMap)
{
  Initialize();
}

void HdAnariRenderDelegate::Initialize()
{
  // Initialize the settings and settings descriptors.
  _settingDescriptors.resize(2);
  _settingDescriptors[0] = {"Ambient Radiance",
      HdAnariRenderSettingsTokens->ambientRadiance,
      VtValue(0.2f)};
  _settingDescriptors[1] = {"Ambient Color",
      HdAnariRenderSettingsTokens->ambientColor,
      VtValue(GfVec3f(0.8f, 0.8f, 0.8f))};
  _PopulateDefaultSettings(_settingDescriptors);

  auto library = anari::loadLibrary("environment", hdAnariDeviceStatusFunc);
  if (!library) {
    TF_RUNTIME_ERROR("failed to load ANARI library from environment");
    return;
  }

  anari::Extensions extensions =
      anari::extension::getDeviceExtensionStruct(library, "default");

  if (!extensions.ANARI_KHR_DEVICE_SYNCHRONIZATION) {
    TF_RUNTIME_ERROR(
        "device doesn't support ANARI_KHR_DEVICE_SYNCHRONIAZATION");
    return;
  }

  auto device = anari::newDevice(library, "default");

  anari::unloadLibrary(library);

  if (!device) {
    TF_RUNTIME_ERROR("failed to create ANARI device");
    return;
  }

  _renderParam = std::make_shared<HdAnariRenderParam>(device);

  std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
  if (_counterResourceRegistry.fetch_add(1) == 0)
    _resourceRegistry = std::make_shared<HdResourceRegistry>();
}

HdAnariRenderDelegate::~HdAnariRenderDelegate()
{
  // Clean the resource registry only when it is the last Anari delegate
  {
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
    if (_counterResourceRegistry.fetch_sub(1) == 1)
      _resourceRegistry.reset();
  }

  _renderParam.reset();
}

HdRenderSettingDescriptorList
HdAnariRenderDelegate::GetRenderSettingDescriptors() const
{
  return _settingDescriptors;
}

HdAnariRenderParam *HdAnariRenderDelegate::GetRenderParam() const
{
  return _renderParam.get();
}

void HdAnariRenderDelegate::CommitResources(HdChangeTracker *tracker) {}

TfTokenVector const &HdAnariRenderDelegate::GetSupportedRprimTypes() const
{
  return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const &HdAnariRenderDelegate::GetSupportedSprimTypes() const
{
  return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const &HdAnariRenderDelegate::GetSupportedBprimTypes() const
{
  return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr HdAnariRenderDelegate::GetResourceRegistry() const
{
  return _resourceRegistry;
}

HdAovDescriptor HdAnariRenderDelegate::GetDefaultAovDescriptor(
    TfToken const &name) const
{
  if (name == HdAovTokens->color) {
    return HdAovDescriptor(HdFormatFloat32Vec4, true, VtValue(GfVec4f(0.0f)));
  } else if (name == HdAovTokens->depth) {
    return HdAovDescriptor(HdFormatFloat32, false, VtValue(1.0f));
  } else if (name == HdAovTokens->cameraDepth) {
    return HdAovDescriptor(HdFormatFloat32, false, VtValue(0.0f));
  } else if (name == HdAovTokens->primId || name == HdAovTokens->instanceId
      || name == HdAovTokens->elementId) {
    return HdAovDescriptor(HdFormatInt32, false, VtValue(-1));
#if 0
  } else {
    HdParsedAovToken aovId(name);
    if (aovId.isPrimvar) {
      return HdAovDescriptor(
          HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
    }
#endif
  }

  return HdAovDescriptor();
}

VtDictionary HdAnariRenderDelegate::GetRenderStats() const
{
  VtDictionary stats;
#if 0
  stats[HdPerfTokens->numCompletedSamples.GetString()] =
      _renderer.GetCompletedSamples();
#endif
  return stats;
}

bool HdAnariRenderDelegate::IsPauseSupported() const
{
  return false;
}

bool HdAnariRenderDelegate::Pause()
{
  return true;
}

bool HdAnariRenderDelegate::Resume()
{
  return true;
}

HdRenderPassSharedPtr HdAnariRenderDelegate::CreateRenderPass(
    HdRenderIndex *index, HdRprimCollection const &collection)
{
  return HdRenderPassSharedPtr(
      new HdAnariRenderPass(index, collection, _renderParam));
}

HdInstancer *HdAnariRenderDelegate::CreateInstancer(
    HdSceneDelegate *delegate, SdfPath const &id)
{
  return new HdAnariInstancer(delegate, id);
}

void HdAnariRenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
  delete instancer;
}

HdRprim *HdAnariRenderDelegate::CreateRprim(
    TfToken const &typeId, SdfPath const &rprimId)
{
  anari::Device d = _renderParam ? _renderParam->GetANARIDevice() : nullptr;

  if (typeId == HdPrimTypeTokens->mesh)
    return new HdAnariMesh(d, rprimId);
  // else if (typeId == HdPrimTypeTokens->points) {
  //   return new HdAnariPoints(d, rprimId);
  // }

  TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());

  return nullptr;
}

void HdAnariRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
  delete rPrim;
}

HdSprim *HdAnariRenderDelegate::CreateSprim(
    TfToken const &typeId, SdfPath const &sprimId)
{
  anari::Device d = _renderParam ? _renderParam->GetANARIDevice() : nullptr;

  if (typeId == HdPrimTypeTokens->camera)
    return new HdCamera(sprimId);
  else if (typeId == HdPrimTypeTokens->material) {
    switch (_renderParam->GetMaterialType()) {
    case HdAnariRenderParam::MaterialType::PhysicallyBased: {
      return new HdAnariPhysicallyBasedMaterial(d, sprimId);
    }
    default: {
      return new HdAnariMatteMaterial(d, sprimId);
    }
    }
  } else if (typeId == HdPrimTypeTokens->extComputation)
    return new HdExtComputation(sprimId);

  TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());

  return nullptr;
}

HdSprim *HdAnariRenderDelegate::CreateFallbackSprim(TfToken const &typeId)
{
  anari::Device d = _renderParam ? _renderParam->GetANARIDevice() : nullptr;

  if (typeId == HdPrimTypeTokens->camera)
    return new HdCamera(SdfPath::EmptyPath());
  else if (typeId == HdPrimTypeTokens->material)
    return new HdAnariMatteMaterial(d, SdfPath::EmptyPath());
  else if (typeId == HdPrimTypeTokens->extComputation)
    return new HdExtComputation(SdfPath::EmptyPath());

  TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());

  return nullptr;
}

void HdAnariRenderDelegate::DestroySprim(HdSprim *sPrim)
{
  delete sPrim;
}

HdBprim *HdAnariRenderDelegate::CreateBprim(
    TfToken const &typeId, SdfPath const &bprimId)
{
  if (typeId == HdPrimTypeTokens->renderBuffer)
    return new HdAnariRenderBuffer(bprimId);

  TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());

  return nullptr;
}

HdBprim *HdAnariRenderDelegate::CreateFallbackBprim(TfToken const &typeId)
{
  if (typeId == HdPrimTypeTokens->renderBuffer)
    return new HdAnariRenderBuffer(SdfPath::EmptyPath());

  TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());

  return nullptr;
}

void HdAnariRenderDelegate::DestroyBprim(HdBprim *bPrim)
{
  delete bPrim;
}

PXR_NAMESPACE_CLOSE_SCOPE
