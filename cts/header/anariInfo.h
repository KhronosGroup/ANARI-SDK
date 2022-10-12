#pragma once
#include <functional>
#include <string>


namespace cts {
void queryInfo(const std::string &library,
               std::optional<std::string> typeFilter,
               std::optional<std::string> subtypeFilter,
               bool skipParameters,
               bool info,
               const std::function<void(const std::string message)> &callback);
}