#pragma once 

#include <string>
#include <optional>
#include <vector>

namespace cts {
    void initANARI(const std::string &libraryName);
    std::vector<std::vector<std::byte>> renderScenes(const std::string &libraryName,
        const std::optional<std::string> &device,
        const std::string &renderer,
        const std::vector<std::string> &scenes);
    std::vector<std::tuple<std::string, bool>> checkCoreExtensions(
        const std::string &libraryName,
        const std::optional<std::string> &device);
    } // namespace cts
