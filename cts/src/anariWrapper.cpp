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

} // namespace cts
