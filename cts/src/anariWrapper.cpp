#include "anariWrapper.h"

#define ANARI_FEATURE_UTILITY_IMPL
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/ext/std.h>

namespace cts {

    void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
    {
      auto& logger = *reinterpret_cast<const std::function<void(const std::string message)>*>(userData);
      (void)device;
      (void)source;
      (void)sourceType;
      (void)code;
      if (severity == ANARI_SEVERITY_FATAL_ERROR) {
        logger("[FATAL] " + std::string(message) + "\n");
      } else if (severity == ANARI_SEVERITY_ERROR) {
        logger("[ERROR] " + std::string(message) + "\n");
      } else if (severity == ANARI_SEVERITY_WARNING) {
        logger("[WARN ] " + std::string(message) + "\n");
      } else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
        logger("[PERF ] " + std::string(message) + "\n");
      } else if (severity == ANARI_SEVERITY_INFO) {
        logger("[INFO ] " + std::string(message) + "\n");
      } else if (severity == ANARI_SEVERITY_DEBUG) {
        logger("[DEBUG] " + std::string(message) + "\n");
      }
    }

    void initANARI(const std::string &libraryName, const std::function<void(const std::string message)>& callback)
    {
      callback("Initialize ANARI...");
      // anari::Library m_debug = anari::loadLibrary("debug", statusFunc);
      try {
        anari::Library lib =
            anari::loadLibrary(libraryName.c_str(), statusFunc, &callback);

        ANARIDevice w = anariNewDevice(lib, "default");

        // ANARIDevice d = anariNewDevice(m_debug, "debug");
      } catch (...) {
        callback("ERROR2");
      }
    }

    std::vector<std::vector<std::byte>> renderScenes(const std::string &libraryName,
        const std::optional<std::string>& device, const std::string& renderer,
        const std::vector<std::string> &scenes,
        const std::function<void(const std::string message)> &callback)
    {
      cts::initANARI(libraryName, callback);

      return {};
    }

    std::vector<std::tuple<std::string, bool>> checkCoreExtensions(
        const std::string &libraryName,
        const std::optional<std::string> &device,
        const std::function<void(const std::string message)> &callback)
    {
      std::string deviceName = "default";
      if (device.has_value()) {
        deviceName = device.value();
      }

      anari::Library lib = anari::loadLibrary(libraryName.c_str(), statusFunc, &callback);

      std::vector<std::tuple<std::string, bool>> result;

      ANARIFeatures features;
      if (anariGetObjectFeatures(&features, lib, deviceName.c_str(), "default", ANARI_DEVICE)) {
        printf("WARNING: library didn't return feature list\n");
        return result;
      }

      result.push_back({"ANARI_extended_device", features.ANARI_extended_device});

      result.push_back({"ANARI_CORE_OBJECTS", features.ANARI_CORE_OBJECTS});

      result.push_back({"ANARI_KHR_CAMERA_OMNIDIRECTIONAL",
          features.ANARI_KHR_CAMERA_OMNIDIRECTIONAL});

      result.push_back({"ANARI_KHR_CAMERA_ORTHOGRAPHIC",
          features.ANARI_KHR_CAMERA_ORTHOGRAPHIC});

      result.push_back({"ANARI_KHR_CAMERA_PERSPECTIVE",
          features.ANARI_KHR_CAMERA_PERSPECTIVE});

      result.push_back({"ANARI_KHR_GEOMETRY_CONE", features.ANARI_KHR_GEOMETRY_CONE});

      result.push_back({"ANARI_KHR_GEOMETRY_CURVE", features.ANARI_KHR_GEOMETRY_CURVE});

      result.push_back({"ANARI_KHR_GEOMETRY_CYLINDER",
          features.ANARI_KHR_GEOMETRY_CYLINDER});

      result.push_back({"ANARI_KHR_GEOMETRY_QUAD", features.ANARI_KHR_GEOMETRY_QUAD});

      result.push_back({"ANARI_KHR_GEOMETRY_SPHERE", features.ANARI_KHR_GEOMETRY_SPHERE});

      result.push_back({"ANARI_KHR_GEOMETRY_TRIANGLE",
          features.ANARI_KHR_GEOMETRY_TRIANGLE});

      result.push_back({"ANARI_KHR_LIGHT_DIRECTIONAL",
          features.ANARI_KHR_LIGHT_DIRECTIONAL});

      result.push_back({"ANARI_KHR_LIGHT_POINT", features.ANARI_KHR_LIGHT_POINT});

      result.push_back({"ANARI_KHR_LIGHT_SPOT", features.ANARI_KHR_LIGHT_SPOT});

      result.push_back({"ANARI_KHR_MATERIAL_MATTE", features.ANARI_KHR_MATERIAL_MATTE});

      result.push_back({"ANARI_KHR_MATERIAL_TRANSPARENT_MATTE",
          features.ANARI_KHR_MATERIAL_TRANSPARENT_MATTE});

      result.push_back({"ANARI_KHR_SAMPLER_IMAGE1D", features.ANARI_KHR_SAMPLER_IMAGE1D});

      result.push_back({"ANARI_KHR_SAMPLER_IMAGE2D", features.ANARI_KHR_SAMPLER_IMAGE2D});

      result.push_back(
          {"ANARI_KHR_SAMPLER_IMAGE3D", features.ANARI_KHR_SAMPLER_IMAGE3D});

      result.push_back({"ANARI_KHR_SAMPLER_PRIMITIVE",
          features.ANARI_KHR_SAMPLER_PRIMITIVE});

      result.push_back({"ANARI_KHR_SAMPLER_TRANSFORM",
          features.ANARI_KHR_SAMPLER_TRANSFORM});

      result.push_back({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          features.ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR});

      result.push_back(
          {"ANARI_KHR_VOLUME_SCIVIS", features.ANARI_KHR_VOLUME_SCIVIS});

      result.push_back({"ANARI_CORE_API", features.ANARI_CORE_API});

      result.push_back({"ANARI_KHR_LIGHT_RING", features.ANARI_KHR_LIGHT_RING});

      result.push_back({"ANARI_KHR_LIGHT_QUAD", features.ANARI_KHR_LIGHT_QUAD});

      result.push_back(
          {"ANARI_KHR_LIGHT_HDRI", features.ANARI_KHR_LIGHT_HDRI});

      result.push_back({"ANARI_SPEC_ALL", features.ANARI_SPEC_ALL});

      result.push_back(
          {"ANARI_KHR_FRAME_CONTINUATION", features.ANARI_KHR_FRAME_CONTINUATION});

      result.push_back(
          {"ANARI_KHR_AUXILIARY_BUFFERS", features.ANARI_KHR_AUXILIARY_BUFFERS});

      result.push_back(
          {"ANARI_KHR_AREA_LIGHTS", features.ANARI_KHR_AREA_LIGHTS});

      result.push_back({"ANARI_KHR_STOCHASTIC_RENDERING",
          features.ANARI_KHR_STOCHASTIC_RENDERING});

      result.push_back({"ANARI_KHR_TRANSFORMATION_MOTION_BLUR",
          features.ANARI_KHR_TRANSFORMATION_MOTION_BLUR});

      result.push_back({"ANARI_KHR_ARRAY1D_REGION", features.ANARI_KHR_ARRAY1D_REGION});

      anari::unloadLibrary(lib);

      return result;
    }

} // namespace cts
