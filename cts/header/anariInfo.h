#pragma once
#include <functional>
#include <string>


namespace cts {
std::string queryInfo(const std::string &library,
               std::optional<std::string> typeFilter,
               std::optional<std::string> subtypeFilter,
               bool skipParameters,
               bool info,
               const std::optional<pybind11::function> &callback);
}
