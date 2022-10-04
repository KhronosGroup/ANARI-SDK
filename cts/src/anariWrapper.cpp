#include "anariWrapper.h"
#define ANARI_FEATURE_UTILITY_IMPL
#include <anari/anari_cpp.hpp>
#include "anari/anari_cpp/ext/std.h"

namespace cts {

    void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
    {
      (void)userData;
      (void)device;
      (void)source;
      (void)sourceType;
      (void)code;
      if (severity == ANARI_SEVERITY_FATAL_ERROR) {
        fprintf(stderr, "[FATAL] %s\n", message);
      } else if (severity == ANARI_SEVERITY_ERROR) {
        fprintf(stderr, "[ERROR] %s\n", message);
      } else if (severity == ANARI_SEVERITY_WARNING) {
        fprintf(stderr, "[WARN ] %s\n", message);
      } else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
        fprintf(stderr, "[PERF ] %s\n", message);
      } else if (severity == ANARI_SEVERITY_INFO) {
        fprintf(stderr, "[INFO ] %s\n", message);
      } else if (severity == ANARI_SEVERITY_DEBUG) {
        fprintf(stderr, "[DEBUG] %s\n", message);
      }
    }

    void initANARI(const std::string &libraryName)
    {
      printf("initialize ANARI...");
      // anari::Library m_debug = anari::loadLibrary("debug", statusFunc);
      try {
        anari::Library lib =
            anari::loadLibrary(libraryName.c_str(), statusFunc);

        anari::Features features = anari::feature::getObjectFeatures(
            lib, "default", "default", ANARI_DEVICE);

        if (!features.ANARI_KHR_GEOMETRY_TRIANGLE)
          printf(
              "WARNING: device doesn't support ANARI_KHR_GEOMETRY_TRIANGLE\n");
        if (!features.ANARI_KHR_CAMERA_PERSPECTIVE)
          printf(
              "WARNING: device doesn't support ANARI_KHR_CAMERA_PERSPECTIVE\n");
        if (!features.ANARI_KHR_LIGHT_DIRECTIONAL)
          printf(
              "WARNING: device doesn't support ANARI_KHR_LIGHT_DIRECTIONAL\n");
        if (!features.ANARI_KHR_MATERIAL_MATTE)
          printf("WARNING: device doesn't support ANARI_KHR_MATERIAL_MATTE\n");

        ANARIDevice w = anariNewDevice(lib, "default");

        // ANARIDevice d = anariNewDevice(m_debug, "debug");
      } catch (...) {
        printf("ERROR2");
      }
    }

    std::vector<std::vector<std::byte>> renderScenes(const std::string &libraryName,
        std::optional<std::string> device,
        std::vector<std::string> scenes)
    {
      cts::initANARI(libraryName);

      return {};
    }

} // namespace cts
