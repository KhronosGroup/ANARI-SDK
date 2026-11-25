// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "common/platform/Platform.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

// glibc 2.39's <sys/pidfd.h> has no extern "C" guard of its own
extern "C" {
#include <sys/pidfd.h>
}

extern char **environ;

namespace anari_guard {
namespace platform {

bool makeChannelPair(int &adapterEnd, int &hostEnd)
{
  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0)
    return false;
  adapterEnd = fds[0];
  hostEnd = fds[1];
  return true;
}

bool sendAll(int endpoint, const void *data, size_t size)
{
  auto *p = static_cast<const char *>(data);
  while (size > 0) {
    ssize_t n = send(endpoint, p, size, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    p += n;
    size -= size_t(n);
  }
  return true;
}

bool recvAll(int endpoint, void *data, size_t size)
{
  auto *p = static_cast<char *>(data);
  while (size > 0) {
    ssize_t n = recv(endpoint, p, size, 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (n == 0)
      return false;
    p += n;
    size -= size_t(n);
  }
  return true;
}

bool sendWithDescriptor(
    int endpoint, const void *data, size_t size, int descriptor)
{
  if (size == 0 || descriptor < 0)
    return false;

  iovec io = {const_cast<void *>(data), size};
  alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))];
  std::memset(control, 0, sizeof(control));
  msghdr msg{};
  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);
  cmsghdr *c = CMSG_FIRSTHDR(&msg);
  c->cmsg_level = SOL_SOCKET;
  c->cmsg_type = SCM_RIGHTS;
  c->cmsg_len = CMSG_LEN(sizeof(int));
  std::memcpy(CMSG_DATA(c), &descriptor, sizeof(int));

  ssize_t n;
  do
    n = sendmsg(endpoint, &msg, MSG_NOSIGNAL);
  while (n < 0 && errno == EINTR);
  if (n < 0)
    return false;
  // The descriptor went with the first byte; the rest is plain data.
  return sendAll(
      endpoint, static_cast<const char *>(data) + n, size - size_t(n));
}

bool recvAll(int endpoint, void *data, size_t size, int &descriptor)
{
  descriptor = -1;
  auto *p = static_cast<char *>(data);
  while (size > 0) {
    iovec io = {p, size};
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))];
    msghdr msg{};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    ssize_t n = recvmsg(endpoint, &msg, MSG_CMSG_CLOEXEC);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (n == 0)
      break;

    for (cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
      if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS)
        continue;
      int fd = -1;
      std::memcpy(&fd, CMSG_DATA(c), sizeof(int));
      if (descriptor < 0)
        descriptor = fd;
      else
        close(fd); // more than was asked for
    }
    p += n;
    size -= size_t(n);
  }
  if (size > 0) {
    closeDescriptor(descriptor);
    descriptor = -1;
    return false;
  }
  return true;
}

bool waitReadable(int endpoint, uint64_t microseconds)
{
  pollfd fd = {endpoint, POLLIN, 0};
  const timespec timeout = {
      time_t(microseconds / 1000000), long(microseconds % 1000000) * 1000};
  for (;;) {
    int n = ppoll(&fd, 1, &timeout, nullptr);
    if (n < 0 && errno == EINTR)
      continue;
    return n != 0; // a failed poll is left for the receive to report
  }
}

void closeDescriptor(int descriptor)
{
  if (descriptor >= 0)
    close(descriptor);
}

// Regions ////////////////////////////////////////////////////////////////////

// Seen in /proc/<pid>/maps as "/memfd:anari-guard-region".
static const char *REGION_NAME = "anari-guard-region";
static constexpr int REGION_SEALS = F_SEAL_SHRINK | F_SEAL_GROW;

Region::~Region()
{
  reset();
}

Region::Region(Region &&other) noexcept
{
  *this = std::move(other);
}

Region &Region::operator=(Region &&other) noexcept
{
  if (this != &other) {
    reset();
    m_data = std::exchange(other.m_data, nullptr);
    m_size = std::exchange(other.m_size, 0);
    m_descriptor = std::exchange(other.m_descriptor, -1);
  }
  return *this;
}

void Region::reset()
{
  if (m_data)
    munmap(m_data, m_size);
  m_data = nullptr;
  m_size = 0;
  closeDescriptor();
}

void Region::closeDescriptor()
{
  platform::closeDescriptor(m_descriptor);
  m_descriptor = -1;
}

bool Region::create(size_t size, Region &out)
{
  out.reset();
  if (size == 0)
    return false;

  // Kernels before 6.3 reject MFD_NOEXEC_SEAL; one configured to demand it
  // rejects its absence.
  int fd = memfd_create(
      REGION_NAME, MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL);
  if (fd < 0 && errno == EINVAL)
    fd = memfd_create(REGION_NAME, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0)
    return false;

  // Allocating now turns running out of memory into a failure here, not a
  // SIGBUS at whoever first touches a page. Sealing the size means neither
  // side can pull pages out from under the other's mapping.
  int rc;
  do
    rc = posix_fallocate(fd, 0, off_t(size));
  while (rc == EINTR);
  if (rc != 0 || fcntl(fd, F_ADD_SEALS, REGION_SEALS | F_SEAL_SEAL) != 0) {
    close(fd);
    return false;
  }

  void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    close(fd);
    return false;
  }
  out.m_data = data;
  out.m_size = size;
  out.m_descriptor = fd;
  return true;
}

bool Region::map(int descriptor, bool writable, Region &out)
{
  out.reset();
  if (descriptor < 0)
    return false;

  // Only something that cannot be resized is safe to map: F_GET_SEALS fails
  // on anything that is not a memfd.
  int seals = fcntl(descriptor, F_GET_SEALS);
  struct stat st;
  if (seals < 0 || (seals & REGION_SEALS) != REGION_SEALS
      || fstat(descriptor, &st) != 0 || st.st_size <= 0)
    return false;

  const size_t size = size_t(st.st_size);
  void *data = mmap(nullptr,
      size,
      writable ? PROT_READ | PROT_WRITE : PROT_READ,
      MAP_SHARED,
      descriptor,
      0);
  if (data == MAP_FAILED)
    return false;
  out.m_data = data;
  out.m_size = size;
  return true;
}

void shutdownEndpoint(int endpoint)
{
  if (endpoint >= 0)
    shutdown(endpoint, SHUT_RDWR);
}

void closeEndpoint(int endpoint)
{
  if (endpoint >= 0)
    close(endpoint);
}

// The host's ends are dup2()'d onto these numbers in the new process.
static constexpr int HOST_CALL_FD = 3;
static constexpr int HOST_EVENT_FD = 4;

bool spawnHost(const std::string &executable,
    int hostCallEnd,
    int hostEventEnd,
    HostProcess &out,
    std::string &error)
{
  // Move both ends clear of the target numbers so neither dup2 clobbers the
  // other's source.
  int callSrc = fcntl(hostCallEnd, F_DUPFD_CLOEXEC, HOST_EVENT_FD + 1);
  int eventSrc = fcntl(hostEventEnd, F_DUPFD_CLOEXEC, HOST_EVENT_FD + 1);
  if (callSrc < 0 || eventSrc < 0) {
    error = std::string("fcntl: ") + std::strerror(errno);
    closeEndpoint(callSrc);
    closeEndpoint(eventSrc);
    return false;
  }

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, callSrc, HOST_CALL_FD);
  posix_spawn_file_actions_adddup2(&actions, eventSrc, HOST_EVENT_FD);
  posix_spawn_file_actions_addclosefrom_np(&actions, HOST_EVENT_FD + 1);

  // The host starts with no blocked signals and default dispositions,
  // whatever the application has set up.
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  sigset_t none, all;
  sigemptyset(&none);
  sigfillset(&all);
  posix_spawnattr_setsigmask(&attr, &none);
  posix_spawnattr_setsigdefault(&attr, &all);
  posix_spawnattr_setflags(
      &attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

  std::string callArg = std::to_string(HOST_CALL_FD);
  std::string eventArg = std::to_string(HOST_EVENT_FD);
  char *argv[] = {const_cast<char *>(executable.c_str()),
      const_cast<char *>(callArg.c_str()),
      const_cast<char *>(eventArg.c_str()),
      nullptr};

  int pidfd = -1;
  int rc =
      pidfd_spawn(&pidfd, executable.c_str(), &actions, &attr, argv, environ);

  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attr);
  closeEndpoint(callSrc);
  closeEndpoint(eventSrc);

  if (rc != 0) {
    error = "cannot start '" + executable + "': " + std::strerror(rc);
    return false;
  }

  out.watch = pidfd;
  out.pid = pidfd_getpid(pidfd);
  return true;
}

void killHost(const HostProcess &host)
{
  if (host.watch >= 0)
    pidfd_send_signal(host.watch, SIGKILL, nullptr, 0);
}

bool hostEnded(const HostProcess &host, int milliseconds)
{
  pollfd fd = {host.watch, POLLIN, 0};
  for (;;) {
    int n = poll(&fd, 1, milliseconds);
    if (n < 0 && errno == EINTR)
      continue;
    return n > 0;
  }
}

bool hostEnding(const HostProcess &host)
{
  if (host.pid < 0)
    return false;
  // "pid (comm) state ppid pgrp session tty tpgid flags ...": a zombie whose
  // other threads are still going, or a task the kernel has marked as exiting.
  char text[512];
  const std::string path = "/proc/" + std::to_string(host.pid) + "/stat";
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return false;
  const ssize_t n = read(fd, text, sizeof(text) - 1);
  close(fd);
  if (n <= 0)
    return false;
  text[n] = '\0';
  const char *afterName = std::strrchr(text, ')');
  char state = 0;
  unsigned flags = 0;
  if (!afterName
      || std::sscanf(
             afterName + 1, " %c %*d %*d %*d %*d %*d %u", &state, &flags)
          != 2)
    return false;
  constexpr unsigned EXITING = 0x4; // PF_EXITING
  return state == 'Z' || state == 'X' || (flags & EXITING) != 0;
}

HostEnd reapHost(const HostProcess &host)
{
  HostEnd end;
  if (host.watch < 0)
    return end;
  siginfo_t info;
  std::memset(&info, 0, sizeof(info));
  while (waitid(P_PIDFD, id_t(host.watch), &info, WEXITED) != 0) {
    if (errno != EINTR)
      return end; // ECHILD: the application reaped it first
  }
  end.kind = info.si_code == CLD_EXITED ? HostEnd::EXITED : HostEnd::SIGNALED;
  end.value = info.si_status;
  return end;
}

bool endedItself(const HostEnd &end)
{
  return end.kind == HostEnd::EXITED
      || (end.kind == HostEnd::SIGNALED && end.value != SIGKILL);
}

void forgetHost(HostProcess &host)
{
  if (host.watch >= 0)
    close(host.watch);
  host.watch = -1;
  host.pid = -1;
}

Wake waitReadableOrHostEnd(int endpoint, const HostProcess &host)
{
  pollfd fds[2] = {{endpoint, POLLIN, 0}, {host.watch, POLLIN, 0}};
  for (;;) {
    int n = poll(fds, 2, -1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return Wake::FAILED;
    }
    if (fds[0].revents & (POLLIN | POLLHUP | POLLERR))
      return Wake::READABLE;
    if (fds[1].revents)
      return Wake::HOST_ENDED;
  }
}

std::string moduleDirectory()
{
  Dl_info info;
  if (dladdr(reinterpret_cast<void *>(&moduleDirectory), &info) == 0
      || !info.dli_fname)
    return {};
  std::string path = info.dli_fname;
  auto slash = path.rfind('/');
  return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

bool isExecutableFile(const std::string &path)
{
  return access(path.c_str(), X_OK) == 0;
}

} // namespace platform
} // namespace anari_guard
