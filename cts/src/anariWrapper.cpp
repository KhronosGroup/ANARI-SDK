#include "anariWrapper.h"
#include <anari/anari_cpp/ext/std.h>
#include <pybind11/pybind11.h>
#include <anari/anari_cpp.hpp>
#include "anariCTSBackend_queries.h"

typedef union ANARIExtensions_u
{
  ANARIExtensions extension;
  int vec[sizeof(ANARIExtensions) / sizeof(int)];
} ANARIExtensions_u;

namespace cts {

void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  if (!userData) {
    return;
  }
  pybind11::gil_scoped_acquire acq;
  auto &logger = *reinterpret_cast<const pybind11::function *>(userData);
  (void)device;
  (void)source;
  (void)sourceType;
  (void)code;
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    logger("[FATAL] " + std::string(message));
  } else if (severity == ANARI_SEVERITY_ERROR) {
    logger("[ERROR] " + std::string(message));
  } else if (severity == ANARI_SEVERITY_WARNING) {
    logger("[WARN ] " + std::string(message));
  } else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
    logger("[PERF ] " + std::string(message));
  } else if (severity == ANARI_SEVERITY_INFO) {
    logger("[INFO ] " + std::string(message));
  } else if (severity == ANARI_SEVERITY_DEBUG) {
    logger("[DEBUG] " + std::string(message));
  }
  pybind11::gil_scoped_release rel;
}

// returns vector of pairs consisting of the feature name and whether it is
// supported by this libraries' device or default device
std::vector<std::tuple<std::string, bool>> queryExtensions(
    const std::string &libraryName,
    const std::optional<std::string> &device,
    const std::optional<pybind11::function> &callback)
{
  // load library
  anari::Library lib;
  if (callback.has_value()) {
    lib = anari::loadLibrary(
        libraryName.c_str(), statusFunc, &(callback.value()));
  } else {
    lib = anari::loadLibrary(libraryName.c_str(), statusFunc, nullptr);
  }

  if (lib == nullptr) {
    throw std::runtime_error("Library could not be loaded: " + libraryName);
  }

  // get default device name if no device was set
  std::string deviceName;
  if (device.has_value()) {
    deviceName = device.value();
  } else {
    const char **devices = anariGetDeviceSubtypes(lib);
    if (!devices) {
      throw std::runtime_error("No device available");
    }
    deviceName = *devices;
  }

  // query extension
  std::vector<std::tuple<std::string, bool>> result;

  ANARIExtensions extension;
  if (anariGetDeviceExtensionStruct(&extension, lib, deviceName.c_str())) {
    printf("WARNING: library didn't return extension list for device\n");
    return result;
  }

  ANARIExtensions_u test;
  test.extension = extension;

  // compile extension and their availability into a vector of tuples
  const size_t featureCount = sizeof(ANARIExtensions) / sizeof(int);

  const char **featureNamesPointer = query_extensions();
  std::vector<std::string> featureNames(
      featureNamesPointer, featureNamesPointer + featureCount);

  for (int i = 0; i < featureCount; ++i) {
    result.push_back({featureNames[i], test.vec[i]});
  }

  anari::unloadLibrary(lib);

  return result;
}

std::string getDefaultDeviceName(const std::string &libraryName,
    const std::optional<pybind11::function> &callback)
{
  anari::Library lib;
  if (callback.has_value()) {
    lib = anari::loadLibrary(
        libraryName.c_str(), statusFunc, &(callback.value()));
  } else {
    lib = anari::loadLibrary(libraryName.c_str(), statusFunc, nullptr);
  }

  if (lib == nullptr) {
    throw std::runtime_error("Library could not be loaded: " + libraryName);
  }

  const char **devices = anariGetDeviceSubtypes(lib);
  if (!devices) {
    return "No device present";
  }
  return *devices;
}

SceneGeneratorWrapper::SceneGeneratorWrapper(const std::string &library,
    const std::optional<std::string> &device,
    const std::optional<pybind11::function> &callback)
{
  // load library
  m_callback = callback;
  if (m_callback.has_value()) {
    m_library =
        anari::loadLibrary(library.c_str(), statusFunc, &(m_callback.value()));
  } else {
    m_library = anari::loadLibrary(library.c_str(), statusFunc, nullptr);
  }

  if (m_library == nullptr) {
    throw std::runtime_error("Library could not be loaded: " + library);
  }

  // get default device name if no device was set
  std::string deviceName;
  if (device.has_value()) {
    deviceName = device.value();
  } else {
    const char **devices = anariGetDeviceSubtypes(m_library);
    if (!devices) {
      throw std::runtime_error("No device available");
    }
    deviceName = *devices;
  }

  // setup device
  anari::Device dev = anari::newDevice(m_library, deviceName.c_str());
  if (dev == nullptr) {
    throw std::runtime_error("Device could not be created: " + deviceName);
  }

  // create a SceneGenerator
  m_sceneGenerator = std::make_unique<anari::scenes::SceneGenerator>(dev);

  anari::release(dev, dev);
}

SceneGeneratorWrapper::SceneGeneratorWrapper(pybind11::function &callback)
{
  // this constructor will wrap the sink device in the debug device to generate debug
  // output on render without actually rendering

  // load libraries
  m_callback = callback;
  m_wrappedLibrary = anariLoadLibrary("sink", statusFunc, &m_callback);
  m_library = anari::loadLibrary("debug", statusFunc, &m_callback);

  if (m_library == nullptr) {
    throw std::runtime_error("Debug library could not be loaded");
  }
  if (m_wrappedLibrary == nullptr) {
    throw std::runtime_error("Sink library could not be loaded");
  }

  // setup devices
  anari::Device wrappedDevice = anariNewDevice(m_wrappedLibrary, "default");
  anari::Device device = anariNewDevice(m_library, "debug");

  if (device == nullptr) {
    throw std::runtime_error("Debug device of the debug library could not be created");
  }
  if (wrappedDevice == nullptr) {
    throw std::runtime_error("Default device of the sink library could not be created");
  }

  anariSetParameter(
      device, device, "wrappedDevice", ANARI_DEVICE, &wrappedDevice);

  anariCommitParameters(device, device);
  anariRelease(wrappedDevice, wrappedDevice);

  // create a SceneGenerator
  m_sceneGenerator = std::make_unique<anari::scenes::SceneGenerator>(device);

  anariRelease(device, device);
}

SceneGeneratorWrapper::~SceneGeneratorWrapper()
{
  if (m_library != nullptr) {
    anari::unloadLibrary(m_library);
  }
  // unload second lib as well if exists
  if (m_wrappedLibrary != nullptr) {
    anari::unloadLibrary(m_wrappedLibrary);
  }
}

} // namespace cts
