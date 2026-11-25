// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "adapter/HostConnection.h"

#include <cstdlib>

namespace anari_guard {

bool resolveWrappedDevice(
    const char *subtype, WrappedDevice &out, std::string &error)
{
  std::string identifier = subtype ? subtype : "default";
  if (identifier == "default") {
    const char *env = std::getenv("ANARI_GUARD_WRAPPED_DEVICE");
    if (!env || !*env) {
      error =
          "no wrapped device named: pass '[subtype@]library' as the "
          "device subtype or set ANARI_GUARD_WRAPPED_DEVICE";
      return false;
    }
    identifier = env;
  }

  auto at = identifier.find('@');
  if (at == std::string::npos)
    out.library = identifier;
  else {
    out.subtype = identifier.substr(0, at);
    out.library = identifier.substr(at + 1);
  }

  if (out.library.empty() || out.subtype.empty()
      || out.library.find('@') != std::string::npos) {
    error = "'" + identifier
        + "' is not a wrapped device identifier ('[subtype@]library')";
    return false;
  }
  if (out.library == "guard") {
    error = "the guard device cannot wrap itself";
    return false;
  }
  return true;
}

static bool findHost(std::string &path, std::string &error)
{
  if (const char *env = std::getenv("ANARI_GUARD_HOST")) {
    path = env;
    if (platform::isExecutableFile(path))
      return true;
    error = "ANARI_GUARD_HOST names '" + path + "', which cannot be executed";
    return false;
  }

  const std::string dir = platform::moduleDirectory();
  for (const char *relative : {"/anariGuardHost", "/../bin/anariGuardHost"}) {
    path = dir + relative;
    if (platform::isExecutableFile(path))
      return true;
  }
  error = "anariGuardHost not found beside the guard library in '" + dir
      + "' or in its '../bin'";
  return false;
}

bool HostConnection::start(std::string &error)
{
  std::string executable;
  if (!findHost(executable, error))
    return false;

  int hostCall = -1, hostEvent = -1;
  if (!platform::makeChannelPair(call, hostCall)
      || !platform::makeChannelPair(event, hostEvent)) {
    error = "cannot create the channels to the host";
    platform::closeEndpoint(hostCall);
    closeEndpoints();
    return false;
  }

  bool spawned =
      platform::spawnHost(executable, hostCall, hostEvent, process, error);
  platform::closeEndpoint(hostCall);
  platform::closeEndpoint(hostEvent);
  if (!spawned) {
    closeEndpoints();
    return false;
  }

  MessageHeader header;
  Buffer payload;
  if (!recvMessage(call, header, payload) || Op(header.opcode) != Op::HELLO) {
    error = "'" + executable + "' ended before saying hello";
    kill();
    return false;
  }
  Reader r(payload);
  uint32_t version = r.pod<uint32_t>();
  if (!r.ok() || version != PROTOCOL_VERSION) {
    error = "'" + executable + "' speaks protocol version "
        + std::to_string(version) + ", this guard library "
        + std::to_string(PROTOCOL_VERSION);
    kill();
    return false;
  }
  return true;
}

void HostConnection::kill()
{
  platform::killHost(process);
  platform::reapHost(process);
  closeEndpoints();
}

void HostConnection::closeEndpoints()
{
  platform::closeEndpoint(call);
  platform::closeEndpoint(event);
  call = event = -1;
  platform::forgetHost(process);
}

} // namespace anari_guard
