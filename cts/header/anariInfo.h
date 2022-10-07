#pragma once
#include <functional>
#include <string>


namespace cts {
void queryInfo(const std::string &library,
    const std::function<void(const std::string message)> &callback);
}