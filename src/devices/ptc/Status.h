// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari.h>
// std
#include <stdexcept>
#include <string>

namespace anari_ptc {

/*! the status code of a message without a more specific one */
inline ANARIStatusCode defaultStatusCode(ANARIStatusSeverity severity)
{
  return severity <= ANARI_SEVERITY_ERROR ? ANARI_STATUS_UNKNOWN_ERROR
                                          : ANARI_STATUS_NO_ERROR;
}

/*! an error PTCDevice reports with this status code, instead of
    ANARI_STATUS_UNKNOWN_ERROR */
struct StatusError : public std::runtime_error
{
  StatusError(ANARIStatusCode code, const std::string &what)
      : std::runtime_error(what), code(code)
  {}
  ANARIStatusCode code;
};

} // namespace anari_ptc
