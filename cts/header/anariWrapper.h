#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <anari/anari.h>
#include "anari/anari_feature_utility.h"

namespace cts {
void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message);

void initANARI(const std::string &libraryName,
    const std::function<void(const std::string message)> &callback);

std::vector<std::tuple<std::string, bool>> queryFeatures(
    const std::string &libraryName,
    const std::optional<std::string> &device,
    const std::optional<std::function<void(const std::string message)>>
        &callback);
} // namespace cts
