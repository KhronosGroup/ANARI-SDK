// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mdlRegistry.h"

#include <mi/base/enums.h>
#include <mi/base/handle.h>
#include <mi/base/ilogger.h>
#include <mi/neuraylib/factory.h>
#include <mi/neuraylib/iarray.h>
#include <mi/neuraylib/idatabase.h>
#include <mi/neuraylib/ifunction_definition.h>
#include <mi/neuraylib/ilogging_configuration.h>
#include <mi/neuraylib/imaterial_instance.h>
#include <mi/neuraylib/imdl_backend.h>
#include <mi/neuraylib/imdl_backend_api.h>
#include <mi/neuraylib/imdl_configuration.h>
#include <mi/neuraylib/imdl_entity_resolver.h>
#include <mi/neuraylib/imdl_execution_context.h>
#include <mi/neuraylib/imdl_factory.h>
#include <mi/neuraylib/imdl_impexp_api.h>
#include <mi/neuraylib/imodule.h>
#include <mi/neuraylib/ineuray.h>
#include <mi/neuraylib/iplugin_api.h>
#include <mi/neuraylib/iplugin_configuration.h>
#include <mi/neuraylib/iscene_element.h>
#include <mi/neuraylib/iscope.h>
#include <mi/neuraylib/istring.h>
#include <mi/neuraylib/itransaction.h>
#include <mi/neuraylib/itype.h>
#include <mi/neuraylib/ivalue.h>
#include <mi/neuraylib/iversion.h>

#include <pxr/base/tf/instantiateSingleton.h>
#include <pxr/base/tf/scoped.h>
#include <pxr/base/tf/singleton.h>
#include <pxr/pxr.h>

#ifdef MI_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <array>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include <pxr/base/tf/diagnostic.h>

using namespace std::string_literals;
using namespace std::string_view_literals;
using mi::base::make_handle;

#ifdef MI_PLATFORM_WINDOWS
#define loadLibrary(s) LoadLibrary(s)
#define freeLibrary(l) FreeLibrary(l)
#define getProcAddress(l, s) GetProcAddress(l, s)
#else
#define loadLibrary(s) dlopen(s, RTLD_LAZY)
#define freeLibrary(l) dlclose(l)
#define getProcAddress(l, s) dlsym(l, s)
#endif

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(HdAnariMdlRegistry);

HdAnariMdlRegistry *HdAnariMdlRegistry::GetInstance()
{
  // Construction never throws (see the constructors): on failure it publishes a
  // disabled instance whose getINeuray() is null. No try/catch here -- swallowing
  // a construction throw would leave TfSingleton wedged (isInitializing set,
  // instance null) and hang every later call.
  return &TfSingleton<HdAnariMdlRegistry>::GetInstance();
}

mi::neuraylib::INeuray *HdAnariMdlRegistry::AcquireNeuray(DllHandle dllHandle)
{
  // Return null (don't throw) on failure: callers build a disabled registry
  // rather than letting an exception escape the singleton ctor.
  if (dllHandle == nullptr) {
    TF_RUNTIME_ERROR("MDL SDK library is not loaded");
    return nullptr;
  }

  // Get neuray main entry point
  auto symbol = getProcAddress(dllHandle, "mi_factory");
  if (symbol == nullptr) {
    TF_RUNTIME_ERROR("Failed to find MDL SDK mi_factory symbol");
    return nullptr;
  }

  auto neuray = mi::neuraylib::mi_factory<mi::neuraylib::INeuray>(symbol);
  if (neuray == nullptr) {
    // INeuray can be acquired only once per process; a null here usually means
    // another subsystem (e.g. the VisRTX device) already holds it, or a version
    // mismatch.
    auto version =
        make_handle(mi::neuraylib::mi_factory<mi::neuraylib::IVersion>(symbol));
    if (!version)
      TF_RUNTIME_ERROR("Cannot get MDL SDK library version");
    else
      TF_RUNTIME_ERROR(
          "Cannot get INeuray: version mismatch or the interface is already "
          "acquired (expected %s, library %s)",
          MI_NEURAYLIB_PRODUCT_VERSION_STRING,
          version->get_product_version());
    return nullptr;
  }

  return neuray;
}

void HdAnariMdlRegistry::ConfigureInstance(
    std::string_view neuraylibpath, mi::base::ILogger *logger)
{
  auto dllHandle = loadLibrary(neuraylibpath.data());
  if (!dllHandle) {
    throw std::runtime_error(
        "Failed to load MDL SDK library "s + std::string(neuraylibpath));
  }

  try {
    auto neuray = HdAnariMdlRegistry::AcquireNeuray(dllHandle);

    new HdAnariMdlRegistry(dllHandle, neuray, logger);
  } catch (std::exception &e) {
    freeLibrary(dllHandle);
    throw std::runtime_error(
        "Failed to acquire neuray instance from the MDL SDK library "s
        + std::string(neuraylibpath));
  }
}

void HdAnariMdlRegistry::ConfigureInstance(
    mi::neuraylib::INeuray *neuray, mi::base::ILogger *logger)
{
  new HdAnariMdlRegistry(nullptr, neuray, logger);
}

HdAnariMdlRegistry::HdAnariMdlRegistry()
    : HdAnariMdlRegistry(loadLibrary("libmdl_sdk" MI_BASE_DLL_FILE_EXT))
{}

HdAnariMdlRegistry::HdAnariMdlRegistry(DllHandle dllHandle)
    : HdAnariMdlRegistry(dllHandle, AcquireNeuray(dllHandle))
{}

void HdAnariMdlRegistry::_Initialize(mi::base::ILogger *logger)
{
  // Get the MDL configuration component so main path can be added.
  auto mdlConfiguration = make_handle(
      m_neuray->get_api_component<mi::neuraylib::IMdl_configuration>());
  mdlConfiguration->add_mdl_system_paths();
  mdlConfiguration->add_mdl_user_paths();

  auto loggingConfig = make_handle(
      m_neuray->get_api_component<mi::neuraylib::ILogging_configuration>());
  if (logger) {
    loggingConfig->set_receiving_logger(logger);
  } else {
    logger = loggingConfig->get_receiving_logger();
  }

  m_logger = logger;

  auto pluginConf = make_handle(
      m_neuray->get_api_component<mi::neuraylib::IPlugin_configuration>());
  if (mi::Sint32 res = pluginConf->load_plugin_library(
          "nv_openimageio" MI_BASE_DLL_FILE_EXT);
      res != 0) {
    if (logger)
      logger->message(mi::base::MESSAGE_SEVERITY_WARNING,
          "plugins",
          "Failed to load the nv_openimageio plugin");
  }
  if (mi::Sint32 res =
          pluginConf->load_plugin_library("dds" MI_BASE_DLL_FILE_EXT);
      res != 0) {
    if (logger)
      logger->message(mi::base::MESSAGE_SEVERITY_WARNING,
          "plugins",
          "Failed to load the dds plugin");
  }
  if (mi::Sint32 res =
          pluginConf->load_plugin_library("mdl_distiller" MI_BASE_DLL_FILE_EXT);
      res != 0) {
    if (logger)
      logger->message(mi::base::MESSAGE_SEVERITY_WARNING,
          "plugins:",
          "Failed to load the mdl_distiller plugin");
  }

  if (m_neuray->get_status() == mi::neuraylib::INeuray::PRE_STARTING
      || m_neuray->get_status() == mi::neuraylib::INeuray::SHUTDOWN) {
    auto result = m_neuray->start();
  }

  // Get the global scope from the database
  auto database =
      make_handle(m_neuray->get_api_component<mi::neuraylib::IDatabase>());
  if (!database.is_valid_interface())
    throw std::runtime_error("Failed to retrieve neuray database component");

  m_globalScope = make_handle(database->get_global_scope());
  if (!m_globalScope.is_valid_interface())
    throw std::runtime_error("Failed to acquire neuray database global scope");

  // Isolate our introspection work from any other consumer of this shared
  // INeuray (e.g. the VisRTX device): use a private child scope rather than
  // the global one.
  m_introspectionScope =
      make_handle(database->create_scope(m_globalScope.get()));
  if (!m_introspectionScope.is_valid_interface())
    throw std::runtime_error("Failed to create MDL introspection scope");

  // Get an execution context for later use.
  m_mdlFactory =
      make_handle(m_neuray->get_api_component<mi::neuraylib::IMdl_factory>());
  if (!m_mdlFactory.is_valid_interface()) {
    throw std::runtime_error("Failed to retrieve MDL factory component");
  }

  m_executionContext = make_handle(m_mdlFactory->create_execution_context());
  if (!m_executionContext.is_valid_interface()) {
    throw std::runtime_error("Failed acquiring an execution context");
  }
}

HdAnariMdlRegistry::HdAnariMdlRegistry(DllHandle dllHandle,
    mi::neuraylib::INeuray *neuray,
    mi::base::ILogger *logger)
    : m_dllHandle(dllHandle), m_neuray(neuray)
{
  // Never let an exception escape this ctor: one that escapes TfSingleton's
  // construction leaves isInitializing=true / instance=null forever, so every
  // later GetInstance() spins (deadlock). On failure publish a disabled
  // registry (getINeuray()==null) and let callers fall back.
  try {
    if (!m_neuray)
      throw std::runtime_error(
          "MDL INeuray unavailable (acquisition failed, or already held by "
          "another subsystem)");
    _Initialize(logger);
  } catch (const std::exception &e) {
    TF_RUNTIME_ERROR("HdAnariMdlRegistry disabled: %s", e.what());
    m_executionContext = {};
    m_mdlFactory = {};
    m_introspectionScope = {};
    m_globalScope = {};
    m_neuray = {};
    if (m_dllHandle) {
      freeLibrary(m_dllHandle);
      m_dllHandle = {};
    }
  }
  TfSingleton<HdAnariMdlRegistry>::SetInstanceConstructed(*this);
}

HdAnariMdlRegistry::~HdAnariMdlRegistry()
{
  m_executionContext = {};
  m_mdlFactory = {};
  m_introspectionScope = {};
  m_globalScope = {};
  if (m_dllHandle && m_neuray) {
    m_neuray->shutdown();
    m_neuray = {};
    freeLibrary(m_dllHandle);
  }
  m_dllHandle = {};
}

auto HdAnariMdlRegistry::createScope(std::string_view scopeName,
    mi::neuraylib::IScope *parent) -> mi::neuraylib::IScope *
{
  auto database =
      make_handle(m_neuray->get_api_component<mi::neuraylib::IDatabase>());

  return database->create_scope(parent);
}

auto HdAnariMdlRegistry::removeScope(mi::neuraylib::IScope *scope) -> void
{
  auto database =
      make_handle(m_neuray->get_api_component<mi::neuraylib::IDatabase>());

  database->remove_scope(scope->get_id());
}

auto HdAnariMdlRegistry::createTransaction(mi::neuraylib::IScope *scope)
    -> mi::neuraylib::ITransaction *
{
  if (!scope)
    scope = m_introspectionScope.get();
  return scope->create_transaction();
}

auto HdAnariMdlRegistry::preprendUserSearchPath(std::string_view path) -> void
{
  auto mdlConfiguration = make_handle(
      m_neuray->get_api_component<mi::neuraylib::IMdl_configuration>());

  auto pathsCount = mdlConfiguration->get_mdl_paths_length();
  std::vector<std::string> paths{std::string(path)};
  for (auto i = 0; i < pathsCount; ++i) {
    auto p = std::string(
        make_handle(mdlConfiguration->get_mdl_path(i))->get_c_str());
    if (p != path)
      paths.push_back(p);
  }
  mdlConfiguration->add_mdl_path(std::string(path).c_str());

  mdlConfiguration->clear_mdl_paths();
  for (auto p : paths) {
    mdlConfiguration->add_mdl_path(p.c_str());
  }
}

auto HdAnariMdlRegistry::loadModuleByCanonicalName(std::string_view filePath,
    mi::neuraylib::ITransaction *transaction) -> const mi::neuraylib::IModule *
{
  std::string path(filePath);
  if (path.find('/') == std::string::npos)
    return {}; // already a module name, nothing to recover

  auto impexpApi = make_handle(
      m_neuray->get_api_component<mi::neuraylib::IMdl_impexp_api>());
  auto mdlConfiguration = make_handle(
      m_neuray->get_api_component<mi::neuraylib::IMdl_configuration>());

  // If the failing path passes through a directory whose basename matches a
  // search root (e.g. ".../vMaterials_2/Concrete/Concrete_Precast.mdl" with a
  // "/data/mdl/vMaterials_2" root), the canonical module name is the tail after
  // it ("::Concrete::Concrete_Precast"). Loading that by name lets the entity
  // resolver pick a complete copy on the search path, with its resources
  // co-located, instead of the incomplete local file.
  auto pathsCount = mdlConfiguration->get_mdl_paths_length();
  for (auto i = decltype(pathsCount)(0); i < pathsCount; ++i) {
    auto rootName =
        std::filesystem::path(
            make_handle(mdlConfiguration->get_mdl_path(i))->get_c_str())
            .filename()
            .string();
    if (rootName.empty())
      continue;

    auto marker = "/" + rootName + "/";
    auto pos = path.rfind(marker);
    if (pos == std::string::npos)
      continue;

    auto tail = path.substr(pos + marker.size());
    if (auto n = tail.size(); n > 4 && tail.substr(n - 4) == ".mdl")
      tail = tail.substr(0, n - 4);
    if (tail.empty())
      continue;

    std::string moduleName = "::";
    for (char c : tail)
      moduleName += (c == '/') ? "::"s : std::string(1, c);

    auto context = make_handle(m_mdlFactory->create_execution_context());
    if (impexpApi->load_module(transaction, moduleName.c_str(), context.get())
        < 0)
      continue;

    auto moduleDbName =
        make_handle(m_mdlFactory->get_db_module_name(moduleName.c_str()));
    if (auto *module = transaction->access<mi::neuraylib::IModule>(
            moduleDbName->get_c_str())) {
      if (m_logger)
        m_logger->message(mi::base::MESSAGE_SEVERITY_INFO,
            "hdanari::mdl",
            ("Recovered '"s + path + "' from the MDL search path as '"
                + moduleName + "'")
                .c_str());
      return module;
    }
  }

  return {};
}

auto HdAnariMdlRegistry::loadModule(std::string_view moduleOrFileName,
    mi::neuraylib::ITransaction *transaction) -> const mi::neuraylib::IModule *
{
  auto impexpApi = make_handle(
      m_neuray->get_api_component<mi::neuraylib::IMdl_impexp_api>());

  auto moduleName = std::string(moduleOrFileName);

  // Check if this is a single MDL name, such as OmniPBR.mdl and
  // resolve it to its equivalent module name, such as ::OmniPBR.
  if (auto len = moduleName.length(); len > 4) {
    auto extension = moduleName.substr(len - 4);
    if (moduleName.find('/') == std::string::npos && extension == ".mdl") {
      moduleName = "::"s + moduleName.substr(0, len - 4);
    }
  }

  // Try and see if the module name can be resolved
  if (auto name =
          make_handle(impexpApi->get_mdl_module_name(moduleName.c_str()));
      name.is_valid_interface()) {
    moduleName = name->get_c_str();
  }

  if (impexpApi->load_module(
          transaction, moduleName.c_str(), m_executionContext.get())
      < 0) {
    // A scene may ship a .mdl without its resources (e.g. a vMaterials module
    // copied without its ./textures), so the local copy fails to compile.
    // A complete copy usually exists on the MDL search path -- recover it by
    // canonical name before giving up.
    if (auto *recovered =
            loadModuleByCanonicalName(moduleOrFileName, transaction))
      return recovered;

    // Surface *why* the module failed, once per module. Otherwise the MDL
    // compiler errors (e.g. C120 unresolved imports from a missing search
    // path) are buried in SDK output and every dependent material only reports
    // a generic "parser returned null" -- 800+ identical lines for one missing
    // package.
    static std::unordered_set<std::string> reported;
    if (reported.insert(moduleName).second) {
      std::string errors;
      for (auto i = 0ull,
                n = m_executionContext->get_error_messages_count();
          i < n;
          ++i) {
        auto message = make_handle(m_executionContext->get_error_message(i));
        errors += "\n  ";
        errors += message->get_string();
      }
      TF_WARN(
          "MDL module '%s' failed to load; dependent materials will be "
          "skipped. Check MDL search paths (MDL_SYSTEM_PATH/MDL_USER_PATH).%s",
          moduleName.c_str(),
          errors.c_str());
    }
    return {};
  }

  // Get the database name for the module we loaded
  auto moduleDbName = make_handle(
      m_mdlFactory->get_db_module_name(std::string(moduleName).c_str()));
  return transaction->access<mi::neuraylib::IModule>(moduleDbName->get_c_str());
}

auto HdAnariMdlRegistry::getFunctionDefinition(
    const mi::neuraylib::IModule *module,
    std::string_view functionName,
    mi::neuraylib::ITransaction *transaction)
    -> const mi::neuraylib::IFunction_definition *
{
  std::string functionQualifiedName;
  if (functionName.back()
      == ')') // Already a full qualified function signature.
    functionQualifiedName = functionName;
  else { // Needs more work to get what we need.
    functionQualifiedName = functionName;
    auto overloads = make_handle(
        module->get_function_overloads(functionQualifiedName.c_str()));
    if (!overloads.is_valid_interface() || overloads->get_length() != 1)
      return {};

    auto firstOverload = make_handle(
        overloads->get_element<mi::IString>(static_cast<mi::Size>(0)));
    functionQualifiedName = firstOverload->get_c_str();
  }

  if (m_logger)
    m_logger->message(mi::base::MESSAGE_SEVERITY_INFO,
        "hdanari::mdl",
        ("Getting function defintiion for "s + functionQualifiedName).c_str());

  return transaction->access<mi::neuraylib::IFunction_definition>(
      functionQualifiedName.c_str());
}

auto HdAnariMdlRegistry::logExecutionContextMessages(
    const mi::neuraylib::IMdl_execution_context *executionContext) -> bool
{
  if (m_logger) {
    for (auto i = 0ull, messageCount = executionContext->get_messages_count();
        i < messageCount;
        ++i) {
      auto message = make_handle(executionContext->get_message(i));
      m_logger->message(message->get_severity(), "misc", message->get_string());
    }

    for (auto i = 0ull,
              messageCount = executionContext->get_error_messages_count();
        i < messageCount;
        ++i) {
      auto message = make_handle(executionContext->get_error_message(i));
      m_logger->message(message->get_severity(), "misc", message->get_string());
    }
  }

  return executionContext->get_error_messages_count() == 0;
}

PXR_NAMESPACE_CLOSE_SCOPE
