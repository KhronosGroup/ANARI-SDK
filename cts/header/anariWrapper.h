#pragma once 

#include <string>
#include <optional>
#include <vector>
#include <functional>

#include <anari/anari.h>

namespace cts {
    void statusFunc(const void *userData,
        ANARIDevice device,
        ANARIObject source,
        ANARIDataType sourceType,
        ANARIStatusSeverity severity,
        ANARIStatusCode code,
        const char *message);

    void initANARI(const std::string &libraryName, const std::function<void(const std::string message)> &callback);

    std::vector<std::vector<std::byte>> renderScenes(const std::string &libraryName,
        const std::optional<std::string> &device,
        const std::string &renderer,
        const std::vector<std::string> &scenes,
        const std::function<void(const std::string message)> &callback);

    std::vector<std::tuple<std::string, bool>> checkCoreExtensions(
        const std::string &libraryName,
        const std::optional<std::string> &device,
        const std::function<void(const std::string message)> &callback);
    } // namespace cts
