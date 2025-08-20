// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "api.h"

#include <mi/base/handle.h>
#include <mi/base/ilogger.h>
#include <mi/base/uuid.h>
#include <mi/neuraylib/icompiled_material.h>
#include <mi/neuraylib/ifunction_definition.h>
#include <mi/neuraylib/iimage.h>
#include <mi/neuraylib/imdl_backend.h>
#include <mi/neuraylib/imdl_backend_api.h>
// #include <mi/neuraylib/imdl_compiler.h>
#include <mi/neuraylib/imdl_execution_context.h>
#include <mi/neuraylib/imdl_factory.h>
#include <mi/neuraylib/imodule.h>
#include <mi/neuraylib/ineuray.h>
#include <mi/neuraylib/iscope.h>
#include <mi/neuraylib/itexture.h>
#include <mi/neuraylib/itransaction.h>
#include <mi/neuraylib/target_code_types.h>

#include <pxr/base/tf/singleton.h>
#include <pxr/pxr.h>
#include <string_view>

PXR_NAMESPACE_OPEN_SCOPE

class HDANARI_MDL_API HdAnariMdlRegistry
{
  friend class TfSingleton<HdAnariMdlRegistry>;

 public:
  HDANARI_MDL_API static void ConfigureInstance(
      std::string_view neuraylibpath = "libmdl_sdk" MI_BASE_DLL_FILE_EXT,
      mi::base::ILogger *logger = nullptr);
  HDANARI_MDL_API static void ConfigureInstance(mi::base::ILogger *logger);
  HDANARI_MDL_API static void ConfigureInstance(
      mi::neuraylib::INeuray *neuray, mi::base::ILogger *logger = nullptr);

  HDANARI_MDL_API static HdAnariMdlRegistry *GetInstance();

  // The main neuray interface can only be acquired once. Make sure it can be
  // shared if taken from there. The original subsystem keeps the ownership of
  // the returned value.
  auto getINeuray() const -> mi::neuraylib::INeuray *
  {
    return m_neuray.get();
  }

  auto getMdlFactory() const -> mi::neuraylib::IMdl_factory *
  {
    return m_mdlFactory.get();
  }

  auto createScope(std::string_view scopeName,
      mi::neuraylib::IScope *parent = {}) -> mi::neuraylib::IScope *;
  auto removeScope(mi::neuraylib::IScope *scope) -> void;
  auto createTransaction(mi::neuraylib::IScope *scope = {})
      -> mi::neuraylib::ITransaction *;

  auto preprendUserSearchPath(std::string_view path) -> void;
  auto loadModule(std::string_view moduleOrFileName,
      mi::neuraylib::ITransaction *transaction)
      -> const mi::neuraylib::IModule *;

  auto getFunctionDefinition(const mi::neuraylib::IModule *module,
      std::string_view functionName,
      mi::neuraylib::ITransaction *transaction)
      -> const mi::neuraylib::IFunction_definition *;

 private:
#ifdef MI_PLATFORM_WINDOWS
  using DllHandle = HANDLE;
#else
  using DllHandle = void *;
#endif

  HDANARI_MDL_API static mi::neuraylib::INeuray *AcquireNeuray(
      DllHandle dllHandle);

  // The main neuray interface can only be acquired once. Possibly get it
  // as a parameter instead of allocating it internally.
  // Note that we allow overriding the logger only if we own the
  // neuray instance, otherwise we assume logging is already taken care of
  HdAnariMdlRegistry();
  HdAnariMdlRegistry(DllHandle dllHandle);
  HdAnariMdlRegistry(DllHandle dllHandle,
      mi::neuraylib::INeuray *neuray,
      mi::base::ILogger *logger = {});

  ~HdAnariMdlRegistry();

  DllHandle m_dllHandle;
  mi::base::Handle<mi::neuraylib::INeuray> m_neuray;
  mi::base::Handle<mi::neuraylib::IScope> m_globalScope;
  mi::base::Handle<mi::neuraylib::IMdl_factory> m_mdlFactory;
  mi::base::Handle<mi::neuraylib::IMdl_execution_context> m_executionContext;
  mi::base::Handle<mi::base::ILogger> m_logger;

  auto logExecutionContextMessages(
      const mi::neuraylib::IMdl_execution_context *executionContext) -> bool;
};

PXR_NAMESPACE_CLOSE_SCOPE
