// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The OS seam: every operating-system facility the guard device uses is
// declared here and implemented once per platform under platform/<os>/.

#include <cstddef>
#include <cstdint>
#include <string>

namespace anari_guard {
namespace platform {

// Channel endpoints //////////////////////////////////////////////////////////

// A connected pair of stream endpoints. Neither end survives an exec unless
// spawnHost() hands it over.
bool makeChannelPair(int &adapterEnd, int &hostEnd);

// Both return false on end-of-file or any error; neither raises a signal.
bool sendAll(int endpoint, const void *data, size_t size);
bool recvAll(int endpoint, void *data, size_t size);

// As sendAll, with 'descriptor' travelling beside the first byte. The sender
// still owns its descriptor afterwards.
bool sendWithDescriptor(
    int endpoint, const void *data, size_t size, int descriptor);

// As recvAll, taking a descriptor that travelled with these bytes: it is
// stored in 'descriptor', which is left at -1 if there was none. A receive
// that may meet a descriptor must be this one, or the descriptor is lost.
bool recvAll(int endpoint, void *data, size_t size, int &descriptor);

void closeDescriptor(int descriptor);

// Whether a receive on 'endpoint' would return at once (with data, with
// end-of-file or with an error), waiting up to 'microseconds' for that.
bool waitReadable(int endpoint, uint64_t microseconds);

// Fail every receive on 'endpoint' from now on, waking a thread blocked in
// one. The endpoint stays open.
void shutdownEndpoint(int endpoint);

void closeEndpoint(int endpoint);

// Regions ////////////////////////////////////////////////////////////////////

// Memory shared between adapter and host: created by one, named to the other
// by a descriptor sent over a channel, and mapped by both. Its size is fixed
// for good before anyone else sees it. The memory is given back to the system
// when the last mapping and descriptor are gone, so a process that dies
// leaves nothing to clean up. Unmapped when the Region goes out of scope.
class Region
{
 public:
  Region() = default;
  ~Region();
  Region(Region &&other) noexcept;
  Region &operator=(Region &&other) noexcept;
  Region(const Region &) = delete;
  Region &operator=(const Region &) = delete;

  // A new, zero-filled, writable region of 'size' bytes, whose memory is
  // committed here and now. False if the system cannot provide it.
  static bool create(size_t size, Region &out);

  // Map a region received from the other process, all of it. False unless
  // the descriptor names a region whose size can no longer change. The caller
  // keeps (and should close) the descriptor.
  static bool map(int descriptor, bool writable, Region &out);

  void *data() const
  {
    return m_data;
  }
  size_t size() const
  {
    return m_size;
  }
  explicit operator bool() const
  {
    return m_data != nullptr;
  }

  // What to send to the other process: held by a created region until
  // closeDescriptor() or the end of the region, -1 otherwise.
  int descriptor() const
  {
    return m_descriptor;
  }
  void closeDescriptor();
  // Hand the descriptor to the caller, who closes it once it is sent.
  int releaseDescriptor()
  {
    const int descriptor = m_descriptor;
    m_descriptor = -1;
    return descriptor;
  }

  void reset();

 private:
  void *m_data{nullptr};
  size_t m_size{0};
  int m_descriptor{-1};
};

// Host process ///////////////////////////////////////////////////////////////

struct HostProcess
{
  int pid{-1};
  int watch{-1}; // becomes readable when the host process has ended
};

// Start 'executable' as `<executable> <call> <event>`, where the two numbers
// name the host's ends of the channels in the new process. The host inherits
// stdin, stdout, stderr and those two endpoints and nothing else.
bool spawnHost(const std::string &executable,
    int hostCallEnd,
    int hostEventEnd,
    HostProcess &out,
    std::string &error);

// Safe at any time until forgetHost(), whether or not the host still runs.
void killHost(const HostProcess &host);

// Whether the host has ended, waiting up to 'milliseconds' for it to.
bool hostEnded(const HostProcess &host, int milliseconds);

// Whether the host, not yet ended, is known to be on its way: a process with
// much to give back (GPU memory, say) can close its channels long before it
// counts as ended. False when that cannot be told.
bool hostEnding(const HostProcess &host);

// How a host process ended.
struct HostEnd
{
  enum Kind
  {
    UNKNOWN, // someone else reaped it first
    EXITED, // 'value' is the exit code
    SIGNALED // 'value' is the signal
  };
  Kind kind{UNKNOWN};
  int value{0};
};

// Whether the host is known to have ended some other way than killHost()
// ends it.
bool endedItself(const HostEnd &end);

// Wait for the host to end and reap it; fine if someone else already did.
HostEnd reapHost(const HostProcess &host);

// Give up what is held to watch the host.
void forgetHost(HostProcess &host);

enum class Wake
{
  READABLE,
  HOST_ENDED,
  FAILED
};

// Block until 'endpoint' has data (or end-of-file) to read, or the host has
// ended. READABLE wins when both hold, so queued data is drained first.
Wake waitReadableOrHostEnd(int endpoint, const HostProcess &host);

// Locating the host executable ///////////////////////////////////////////////

// Directory holding the module (shared library or executable) this code was
// linked into; empty if unknown.
std::string moduleDirectory();

bool isExecutableFile(const std::string &path);

} // namespace platform
} // namespace anari_guard
