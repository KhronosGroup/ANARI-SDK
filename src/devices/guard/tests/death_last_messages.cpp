// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The wrapped device's last messages come before the death message. The host
// crashes with a caller waiting for a reply, just after the wrapped device
// has said several things; the application's status callback is slow, so the
// caller is awake long before the dispatcher has passed them all on. It
// waits, and then tells of the death itself.

#include "DeathTest.h"
#include "Recording.h"

using namespace guard_test;

struct Order
{
  std::mutex mutex;
  std::vector<std::string> messages;
  std::vector<std::thread::id> threads;
};

static void onStatus(const void *userData,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  auto &order = *static_cast<Order *>(const_cast<void *>(userData));
  const bool fatal = severity == ANARI_SEVERITY_FATAL_ERROR;
  if (!fatal && std::string(message).find("last words") == std::string::npos)
    return;
  if (!fatal)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::lock_guard<std::mutex> lock(order.mutex);
  order.messages.push_back(fatal ? "the death message" : message);
  order.threads.push_back(std::this_thread::get_id());
}

int main()
{
  const std::string log = useRecordingDevice();
  Order order;
  ANARILibrary library = anariLoadLibrary("guard", onStatus, &order);
  CHECK(library);
  setFault("getProperty:1:before:segv");
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);

  const int count = 5;
  for (int i = 0; i < count; i++) {
    const std::string words = "last words " + std::to_string(i);
    anariSetParameter(
        device, device, "emitStatus", ANARI_STRING, words.c_str());
  }
  int32_t answer = 0;
  const auto asked = std::chrono::steady_clock::now();
  CHECK(!anariGetProperty(
      device, device, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT));
  const auto waited = std::chrono::steady_clock::now() - asked;

  // All of it has been said by the time the call returns, in order, and the
  // death message alone on the calling thread.
  {
    std::lock_guard<std::mutex> lock(order.mutex);
    CHECK(order.messages.size() == size_t(count) + 1);
    for (int i = 0; i < count; i++) {
      CHECK(order.messages[i] == "last words " + std::to_string(i));
      CHECK(order.threads[i] != std::this_thread::get_id());
    }
    CHECK(order.messages[count] == "the death message");
    CHECK(order.threads[count] == std::this_thread::get_id());
  }
  // The caller did wait for the dispatcher.
  CHECK(waited >= std::chrono::milliseconds(20 * count));

  anariRelease(device, device);
  anariUnloadLibrary(library);
  CHECK(order.messages.size() == size_t(count) + 1);
  unlink(log.c_str());
  std::printf("passed\n");
  return 0;
}
