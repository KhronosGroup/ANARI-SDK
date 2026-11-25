// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "adapter/GuardDevice.h"
#include "adapter/OutOfScope.h"
#include "common/Tuning.h"

// anari
#include <anari/frontend/type_utility.h>
// std
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace anari_guard {

// Pipelined calls are sent once this many bytes are waiting.
static constexpr size_t FLUSH_THRESHOLD = 64 * 1024;
// Bulk data sent inline goes through the buffer of pipelined calls when it is
// smaller than this, and is otherwise written to the call channel from where
// it is, which measured 10 to 40% faster from here up and no different below.
static constexpr size_t DIRECT_WRITE_MINIMUM = 128 * 1024;

// Values returned by pointer /////////////////////////////////////////////////

void decodeValue(Reader &r, Interned &out)
{
  switch (r.pod<ValueKind>()) {
  case ValueKind::BYTES: {
    uint32_t size = r.pod<uint32_t>();
    if (auto *p = r.bytes(size))
      out.bytes.assign(p, p + size);
    out.value = out.bytes.data();
    break;
  }
  case ValueKind::STRING: {
    out.strings.push_back(r.string());
    out.value = out.strings[0].c_str();
    break;
  }
  case ValueKind::STRING_LIST: {
    uint32_t count = r.pod<uint32_t>();
    for (uint32_t i = 0; i < count && r.ok(); i++)
      out.strings.push_back(r.string());
    for (auto &s : out.strings)
      out.pointers.push_back(s.c_str());
    out.pointers.push_back(nullptr);
    out.value = out.pointers.data();
    break;
  }
  case ValueKind::DATA_TYPE_LIST: {
    uint32_t count = r.pod<uint32_t>();
    for (uint32_t i = 0; i < count && r.ok(); i++)
      out.types.push_back(ANARIDataType(r.pod<int32_t>()));
    out.types.push_back(ANARI_UNKNOWN);
    out.value = out.types.data();
    break;
  }
  case ValueKind::PARAMETER_LIST: {
    uint32_t count = r.pod<uint32_t>();
    for (uint32_t i = 0; i < count && r.ok(); i++) {
      out.strings.push_back(r.string());
      out.types.push_back(ANARIDataType(r.pod<int32_t>()));
    }
    for (size_t i = 0; i < out.strings.size(); i++)
      out.parameters.push_back({out.strings[i].c_str(), out.types[i]});
    out.parameters.push_back({nullptr, ANARI_UNKNOWN});
    out.value = out.parameters.data();
    break;
  }
  case ValueKind::NONE:
  default:
    out.value = nullptr;
  }
}

const void *GuardDevice::intern(Reader &r, bool extensionList)
{
  // Identical content, byte for byte on the wire, is one stored value.
  const size_t size = r.remaining();
  const uint8_t *start = r.bytes(0);
  auto item = std::make_unique<Interned>();
  decodeValue(r, *item);
  if (!r.ok() || !item->value)
    return nullptr;
  std::string key(reinterpret_cast<const char *>(start), size - r.remaining());
  if (extensionList && !item->pointers.empty()) {
    removeOutOfScopeExtensions(item->strings);
    item->pointers.clear();
    for (auto &s : item->strings)
      item->pointers.push_back(s.c_str());
    item->pointers.push_back(nullptr);
    item->value = item->pointers.data();
    key += "extensions"; // not the same value as the unfiltered list
  }

  std::lock_guard<std::mutex> lock(m_tableMutex);
  auto &slot = m_interned[key];
  if (!slot)
    slot = std::move(item);
  return slot->value;
}

// Creation and destruction ///////////////////////////////////////////////////

GuardDevice::GuardDevice(ANARILibrary library,
    const HostConnection &host,
    const WrappedDevice &wrapped)
    : anari::DeviceImpl(library), m_host(host), m_wrapped(wrapped)
{}

GuardDevice *GuardDevice::create(
    ANARILibrary library, const WrappedDevice &wrapped, std::string &error)
{
  HostConnection host;
  if (!host.start(error))
    return nullptr;

  // The threads run from here on, so that whatever the wrapped library says
  // while it loads is drained, and delivered whether or not creation succeeds:
  // as the library's messages, with no device.
  auto *device = new GuardDevice(library, host, wrapped);
  device->startThreads();

  Buffer request;
  {
    Writer w(request, Op::CREATE_DEVICE);
    w.string(wrapped.library);
    w.string(wrapped.subtype);
  }

  MessageHeader header;
  Buffer reply;
  device->countSent(request.size());
  bool ok = platform::sendAll(host.call, request.data(), request.size())
      && device->receive(host.call, header, reply);
  if (!ok)
    error = "the host ended while creating the wrapped device";
  else {
    Reader r(reply);
    ok = r.pod<uint8_t>() != 0;
    std::string failure = r.string();
    if (!ok)
      error = failure;
  }

  if (!ok) {
    device->abandon();
    delete device;
    return nullptr;
  }

  {
    std::lock_guard<std::mutex> lock(device->m_creationMutex);
    device->m_created = true;
  }
  device->m_creationResolved.notify_all();
  return device;
}

void GuardDevice::startThreads()
{
  {
    // The dispatcher takes this lock before anything else.
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_dispatcher = std::thread([this] { dispatchEvents(); });
    m_dispatcherId = m_dispatcher.get_id();
  }
  m_reader = std::thread([this] { readEvents(); });
}

// Creation failed: end the host and let both threads run out.
void GuardDevice::abandon()
{
  {
    std::lock_guard<std::mutex> lock(m_creationMutex);
    m_abandoned = true;
  }
  m_creationResolved.notify_all();
  platform::killHost(m_host.process);
  m_reader.join();
  Event stop;
  stop.stop = true;
  post(std::move(stop));
  m_dispatcher.join();
  platform::reapHost(m_host.process);
  m_host.closeEndpoints();
}

// Devices released from inside a callback. Such a release is counted the
// moment the application makes it, and its dispatcher thread is listed here
// when it has all but finished, so that unloading the library can wait for
// both. Never destroyed: a thread still listed when the process exits is
// simply left.
struct ReleasesInCallbacks
{
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<std::thread::id> unfinished; // the dispatchers still at it
  std::vector<std::thread> threads; // the dispatchers all but done
};

static ReleasesInCallbacks &releasesInCallbacks()
{
  static auto *releases = new ReleasesInCallbacks;
  return *releases;
}

void GuardDevice::waitForReleasesInCallbacks()
{
  auto &releases = releasesInCallbacks();
  std::vector<std::thread> threads;
  {
    std::unique_lock<std::mutex> lock(releases.mutex);
    // Unloading from inside the callback that released the device would wait
    // for itself: that release is left to finish as it can.
    const auto self = std::this_thread::get_id();
    releases.changed.wait(lock, [&] {
      for (auto &id : releases.unfinished) {
        if (id != self)
          return false;
      }
      return true;
    });
    threads.swap(releases.threads);
  }
  for (auto &t : threads) {
    // A library unloaded from inside the very callback cannot wait for it.
    if (t.get_id() == std::this_thread::get_id())
      t.detach();
    else
      t.join();
  }
}

void GuardDevice::releaseHost()
{
  m_releasing = true;
  if (!m_dead) {
    std::lock_guard<std::mutex> lock(m_callMutex);
    {
      Writer w(m_out, Op::RELEASE_DEVICE);
    }
    flush();
    MessageHeader header;
    Buffer reply;
    if (!m_dead && !receive(m_host.call, header, reply))
      markDead();
  }
  // The host closes the event channel after replying, and a dead host's is
  // closed already: either way the reader runs to end-of-file.
  m_reader.join();
  // A host that ended before its reply is device death like any other.
  if (m_dead)
    announceDeath();
}

// The application's last release of the device, from any thread of its own.
// It is a call like any other, so a death not yet told is told before it
// returns, and while the dispatcher can still deliver what came before.
void GuardDevice::releaseDevice()
{
  m_callers++;
  releaseHost();
  bool delivered = false;
  std::exception_ptr thrown;
  if (m_dead)
    thrown = deliverDeath(delivered);
  m_callers--;
  wakeDeathWaiters();

  Event stop;
  stop.stop = true;
  post(std::move(stop));
  m_dispatcher.join();
  platform::reapHost(m_host.process);
  m_host.closeEndpoints();
  delete this;
  if (thrown)
    std::rethrow_exception(thrown);
}

// The same release, made from inside a callback and so on the dispatcher,
// which finishes it here once that callback has returned. Events still queued
// are dropped: the application let go of the device before they were due. So
// is word of a death that comes during the release, which no call is left to
// deliver.
void GuardDevice::finishReleaseOnDispatcher()
{
  releaseHost();
  platform::reapHost(m_host.process);
  m_host.closeEndpoints();
  {
    auto &releases = releasesInCallbacks();
    std::lock_guard<std::mutex> lock(releases.mutex);
    auto &unfinished = releases.unfinished;
    unfinished.erase(std::find(
        unfinished.begin(), unfinished.end(), std::this_thread::get_id()));
    releases.threads.push_back(std::move(m_dispatcher));
    releases.changed.notify_all();
  }
  delete this;
}

// Device death ///////////////////////////////////////////////////////////////

// How long a host whose channel failed is given to be seen ended before the
// adapter ends it. A dying process closes its channels a moment before it
// counts as ended, and that moment must not be mistaken for a host that is
// alive but unreachable.
static constexpr int HOST_END_GRACE_MS = 100;

void GuardDevice::markDead(bool hostSeenEnded)
{
  if (m_dead.exchange(true))
    return;

  // By the time the application hears of it the host process has ended. One
  // that is alive and shows no sign of ending is ended here; one already on
  // its way is waited for when the death is announced.
  if (!hostSeenEnded && !platform::hostEnded(m_host.process, HOST_END_GRACE_MS)
      && !platform::hostEnding(m_host.process)) {
    m_killedByAdapter = true;
    platform::killHost(m_host.process);
  }

  // Wakes a caller waiting for a reply, even if something else still holds
  // the host's end open.
  platform::shutdownEndpoint(m_host.call);
}

void GuardDevice::announceDeath()
{
  if (m_announceStarted.exchange(true))
    return;

  const platform::HostEnd end = platform::reapHost(m_host.process);
  std::string how;
  // A host the adapter had to end is a lost connection, unless it turns out
  // to have been ending already: a host that crashed and is slow to go (one
  // holding a GPU can take longer than the grace it is given) still crashed.
  if (m_killedByAdapter && !platform::endedItself(end))
    how = "the connection to the host running '" + m_wrapped.library
        + "' was lost";
  else {
    how = "the host running '" + m_wrapped.library + "' ";
    if (end.kind == platform::HostEnd::SIGNALED)
      how += "was killed by signal " + std::to_string(end.value);
    else if (end.kind == platform::HostEnd::EXITED)
      how += "exited with code " + std::to_string(end.value);
    else
      how += "ended";
  }

  // The message is not queued: the application's next call on the device
  // delivers it, on that call's thread. What is queued marks how far the
  // dispatcher must get first, so that the wrapped device's last messages
  // come before it.
  {
    std::lock_guard<std::mutex> lock(m_deathMutex);
    m_deathMessage = "guard device: " + how
        + "; the device is dead and its calls now do nothing";
    m_announced = true;
  }
  m_deathChanged.notify_all();
  Event death;
  death.death = true;
  post(std::move(death));

  // Frames in flight get their callback and their completion from the
  // adapter. One whose completion the host did send is already queued, and
  // so is a callback the host did report. A thread waiting on any frame is
  // woken, to deliver the message.
  std::vector<std::pair<ObjectId, std::shared_ptr<FrameState>>> inFlight;
  {
    std::lock_guard<std::mutex> lock(m_tableMutex);
    for (auto &f : m_frames)
      inFlight.push_back(f);
  }
  for (auto &f : inFlight) {
    Event completed;
    bool calledBack = false;
    {
      std::lock_guard<std::mutex> lock(f.second->mutex);
      f.second->changed.notify_all();
      if (!f.second->hostRendering)
        continue;
      f.second->hostRendering = false;
      completed.render = f.second->renders;
      calledBack = f.second->calledBack;
    }
    if (!calledBack) {
      Event callback;
      callback.kind = Op::FRAME_CALLED_BACK_EVENT;
      callback.id = f.first;
      callback.frame = f.second;
      post(std::move(callback));
    }
    completed.kind = Op::FRAME_COMPLETED_EVENT;
    completed.id = f.first;
    completed.frame = f.second;
    post(std::move(completed));
  }
}

void GuardDevice::wakeDeathWaiters()
{
  {
    std::lock_guard<std::mutex> lock(m_deathMutex);
  }
  m_deathChanged.notify_all();
}

std::exception_ptr GuardDevice::deliverDeath(bool &delivered) noexcept
{
  delivered = false;
  if (m_deathClaimed)
    return nullptr;

  // One call delivers: the first to get here with a callback to give the
  // message to. Without one the message waits for the call that commits one.
  ANARIStatusCallback callback = nullptr;
  const void *userData = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    if (m_statusCommitted && m_statusCallback) {
      callback = m_statusCallback;
      userData = m_statusUserData;
    } else {
      callback = defaultStatusCallback();
      userData = defaultStatusCallbackUserPtr();
    }
    if (!callback || m_deathClaimed.exchange(true))
      return nullptr;
  }

  // The wrapped device's last messages come first. A call made from inside
  // a callback is the dispatcher's own: the rest of its queue follows.
  const auto self = std::this_thread::get_id();
  const bool onDispatcher = self == m_dispatcherId;
  {
    std::unique_lock<std::mutex> lock(m_deathMutex);
    m_deathChanged.wait(
        lock, [&] { return onDispatcher ? m_announced : m_drained; });
  }

  // No adapter lock is held: the callback may make any call, exit or throw.
  std::exception_ptr thrown;
  m_deliveringThread = self;
  try {
    callback(userData,
        this_device(),
        this_device(),
        ANARI_DEVICE,
        ANARI_SEVERITY_FATAL_ERROR,
        ANARI_STATUS_UNKNOWN_ERROR,
        m_deathMessage.c_str());
  } catch (...) {
    thrown = std::current_exception();
  }
  m_deliveringThread = std::thread::id();
  {
    std::lock_guard<std::mutex> lock(m_deathMutex);
    m_delivered = true;
  }
  m_deathChanged.notify_all();
  delivered = true;
  return thrown;
}

void GuardDevice::leave()
{
  if (!m_dead) {
    m_callers--;
    if (m_dead)
      wakeDeathWaiters();
    return;
  }
  bool delivered = false;
  std::exception_ptr thrown = deliverDeath(delivered);
  m_callers--;
  wakeDeathWaiters();
  // Released for the last time by the callback this call has just run.
  if (delivered && m_releasedInDelivery)
    releaseDevice();
  if (thrown)
    std::rethrow_exception(thrown);
}

// Handles ////////////////////////////////////////////////////////////////////

ObjectId GuardDevice::idOf(ANARIObject object) const
{
  return handleIsDevice(object) ? DEVICE_ID : ObjectId(uintptr_t(object));
}

ANARIObject GuardDevice::handleOf(ObjectId id) const
{
  return id == DEVICE_ID ? ANARIObject(this_device())
                         : ANARIObject(uintptr_t(id));
}

ObjectId GuardDevice::newId()
{
  const ObjectId id = m_nextId++;
  std::lock_guard<std::mutex> lock(m_tableMutex);
  m_live[id] = 1;
  return id;
}

ObjectId GuardDevice::newInternalId()
{
  return m_nextId++;
}

void GuardDevice::refuse(const char *call, const char *what)
{
  report(DEVICE_ID,
      ANARI_SEVERITY_ERROR,
      ANARI_STATUS_INVALID_ARGUMENT,
      std::string("guard device: ") + call + ": " + what
          + "; the call does nothing");
}

static const char *NOT_A_HANDLE =
    "the object is not one this device created, or was already released";

bool GuardDevice::target(ANARIObject object, const char *call, ObjectId &id)
{
  id = idOf(object);
  if (id == DEVICE_ID)
    return true;
  {
    std::lock_guard<std::mutex> lock(m_tableMutex);
    if (m_live.count(id))
      return true;
  }
  refuse(call, NOT_A_HANDLE);
  return false;
}

std::shared_ptr<GuardDevice::FrameState> GuardDevice::frameFor(
    ANARIFrame frame, const char *call)
{
  auto state = frameState(idOf(frame));
  if (!state)
    refuse(call, "the object is not a frame of this device");
  return state;
}

void GuardDevice::idsOf(void *ids,
    const ANARIObject *handles,
    size_t count,
    const char *call,
    ObjectId *sent,
    bool unchanged)
{
  bool strangers = false;
  {
    std::lock_guard<std::mutex> lock(m_tableMutex);
    auto *out = static_cast<uint8_t *>(ids);
    for (size_t i = 0; i < count; i++) {
      ObjectId id = idOf(handles[i]);
      if (unchanged && id == sent[i]) {
        id = UNCHANGED_ID;
      } else {
        if (id != NULL_ID && id != DEVICE_ID && !m_live.count(id)) {
          id = NULL_ID;
          strangers = true;
        }
        if (sent)
          sent[i] = id;
      }
      std::memcpy(out + i * sizeof(ObjectId), &id, sizeof(id));
    }
  }
  if (strangers) {
    report(DEVICE_ID,
        ANARI_SEVERITY_ERROR,
        ANARI_STATUS_INVALID_ARGUMENT,
        std::string("guard device: ") + call
            + ": the array holds objects this device did not create, or that "
              "were already released; they are passed on as NULL");
  }
}

std::shared_ptr<GuardDevice::FrameState> GuardDevice::frameState(ObjectId id)
{
  std::lock_guard<std::mutex> lock(m_tableMutex);
  auto it = m_frames.find(id);
  return it == m_frames.end() ? nullptr : it->second;
}

// Call channel ///////////////////////////////////////////////////////////////

void GuardDevice::countSent(size_t bytes)
{
  m_bytesSent += bytes;
  m_flushes++;
}

bool GuardDevice::receive(
    int endpoint, MessageHeader &header, Buffer &payload, int *descriptor)
{
  const bool ok = descriptor
      ? recvMessage(endpoint, header, payload, *descriptor)
      : recvMessage(endpoint, header, payload);
  if (ok)
    m_bytesReceived += sizeof(header) + payload.size();
  return ok;
}

bool GuardDevice::newRegion(size_t bytes, platform::Region &out)
{
  if (!platform::Region::create(bytes, out))
    return false;
  m_regionsCreated++;
  return true;
}

bool GuardDevice::counter(const char *name, uint64_t &value) const
{
  static const char prefix[] = "guard.";
  if (std::strncmp(name, prefix, sizeof(prefix) - 1) != 0)
    return false;
  const std::string which = name + sizeof(prefix) - 1;
  if (which == "bytesSent")
    value = m_bytesSent;
  else if (which == "bytesReceived")
    value = m_bytesReceived;
  else if (which == "flushes")
    value = m_flushes;
  else if (which == "regionsCreated")
    value = m_regionsCreated;
  else
    return false;
  return true;
}

void GuardDevice::flush()
{
  if (m_out.empty())
    return;
  countSent(m_out.size());
  if (!platform::sendAll(m_host.call, m_out.data(), m_out.size()))
    markDead();
  m_out.clear(); // on a dead device, dropped
}

void GuardDevice::sendIfFull()
{
  if (m_out.size() >= FLUSH_THRESHOLD)
    flush();
}

template <typename F>
void GuardDevice::send(Op op, F &&fill, bool flushNow)
{
  if (m_dead)
    return;
  std::lock_guard<std::mutex> lock(m_callMutex);
  {
    Writer w(m_out, op);
    fill(w);
  }
  if (flushNow)
    flush();
  else
    sendIfFull();
}

template <typename F>
void GuardDevice::sendWithPayload(
    Op op, F &&fill, const void *payload, size_t bytes)
{
  if (m_dead)
    return;
  const size_t start = m_out.size();
  {
    Writer w(m_out, op);
    fill(w);
    if (bytes < DIRECT_WRITE_MINIMUM) {
      w.bytes(payload, bytes);
      return;
    }
  }
  // The header counts the payload, which follows it on the channel without
  // ever being in the buffer.
  uint32_t size = 0;
  std::memcpy(&size, m_out.data() + start, sizeof(size));
  size += uint32_t(bytes);
  std::memcpy(m_out.data() + start, &size, sizeof(size));
  flush();
  if (m_dead)
    return;
  countSent(bytes);
  if (!platform::sendAll(m_host.call, payload, bytes))
    markDead();
}

template <typename F>
bool GuardDevice::call(Op op, F &&fill, Buffer &reply, int *descriptor)
{
  if (m_dead)
    return false;
  std::lock_guard<std::mutex> lock(m_callMutex);
  {
    Writer w(m_out, op);
    fill(w);
  }
  flush();
  MessageHeader header;
  int received = -1;
  if (m_dead || !receive(m_host.call, header, reply, &received)) {
    markDead();
    reply.clear();
    return false;
  }
  if (descriptor)
    *descriptor = received;
  else
    platform::closeDescriptor(received);
  return true;
}

template <typename F>
void GuardDevice::sendWithDescriptor(Op op, F &&fill, int descriptor)
{
  flush();
  if (m_dead)
    return;
  Buffer message;
  {
    Writer w(message, op, 1);
    fill(w);
  }
  countSent(message.size());
  if (!platform::sendWithDescriptor(
          m_host.call, message.data(), message.size(), descriptor))
    markDead();
}

template <typename F>
const void *GuardDevice::introspect(Op op, F &&fill, bool extensionList)
{
  Buffer request;
  {
    Writer w(request, op);
    fill(w);
  }
  const std::string key(request.begin(), request.end());
  {
    std::lock_guard<std::mutex> lock(m_tableMutex);
    auto it = m_introspection.find(key);
    if (it != m_introspection.end())
      return it->second;
  }

  Buffer reply;
  if (!call(op, fill, reply))
    return nullptr;
  Reader r(reply);
  const void *answer = intern(r, extensionList);
  std::lock_guard<std::mutex> lock(m_tableMutex);
  m_introspection[key] = answer;
  return answer;
}

// Array contents travel as raw bytes, except object arrays, whose handles go
// as object ids.
void GuardDevice::writeElements(Writer &w,
    const void *data,
    ANARIDataType type,
    size_t bytes,
    const char *call,
    ObjectId *sent,
    bool unchanged)
{
  if (!anari::isObject(type)) {
    w.bytes(data, bytes);
    return;
  }
  copyElements(w.reserve(bytes), data, type, bytes, call, sent, unchanged);
}

// The same, into a region.
void GuardDevice::copyElements(void *to,
    const void *from,
    ANARIDataType type,
    size_t bytes,
    const char *call,
    ObjectId *sent,
    bool unchanged)
{
  if (!anari::isObject(type)) {
    std::memcpy(to, from, bytes);
    return;
  }
  static_assert(sizeof(ObjectId) == sizeof(ANARIObject), "ids replace handles");
  idsOf(to,
      static_cast<const ANARIObject *>(from),
      bytes / sizeof(ANARIObject),
      call,
      sent,
      unchanged);
}

// Object creation ////////////////////////////////////////////////////////////

template <typename T>
T GuardDevice::create(Op op, const char *subtype, bool hasSubtype)
{
  ObjectId id = newId();
  send(op, [&](Writer &w) {
    w.pod(id);
    if (hasSubtype)
      w.string(subtype ? subtype : "");
  });
  return T(handleOf(id));
}

ANARIGeometry GuardDevice::newGeometry(const char *type)
{
  Entry entry(this);
  return create<ANARIGeometry>(Op::NEW_GEOMETRY, type);
}

ANARIMaterial GuardDevice::newMaterial(const char *type)
{
  Entry entry(this);
  return create<ANARIMaterial>(Op::NEW_MATERIAL, type);
}

ANARISampler GuardDevice::newSampler(const char *type)
{
  Entry entry(this);
  return create<ANARISampler>(Op::NEW_SAMPLER, type);
}

ANARISurface GuardDevice::newSurface()
{
  Entry entry(this);
  return create<ANARISurface>(Op::NEW_SURFACE, nullptr, false);
}

ANARISpatialField GuardDevice::newSpatialField(const char *type)
{
  Entry entry(this);
  return create<ANARISpatialField>(Op::NEW_SPATIAL_FIELD, type);
}

ANARIVolume GuardDevice::newVolume(const char *type)
{
  Entry entry(this);
  return create<ANARIVolume>(Op::NEW_VOLUME, type);
}

ANARILight GuardDevice::newLight(const char *type)
{
  Entry entry(this);
  return create<ANARILight>(Op::NEW_LIGHT, type);
}

ANARIGroup GuardDevice::newGroup()
{
  Entry entry(this);
  return create<ANARIGroup>(Op::NEW_GROUP, nullptr, false);
}

ANARIInstance GuardDevice::newInstance(const char *type)
{
  Entry entry(this);
  return create<ANARIInstance>(Op::NEW_INSTANCE, type);
}

ANARIWorld GuardDevice::newWorld()
{
  Entry entry(this);
  return create<ANARIWorld>(Op::NEW_WORLD, nullptr, false);
}

ANARICamera GuardDevice::newCamera(const char *type)
{
  Entry entry(this);
  return create<ANARICamera>(Op::NEW_CAMERA, type);
}

ANARIRenderer GuardDevice::newRenderer(const char *type)
{
  Entry entry(this);
  return create<ANARIRenderer>(Op::NEW_RENDERER, type);
}

ANARIFrame GuardDevice::newFrame()
{
  Entry entry(this);
  ObjectId id = newId();
  {
    std::lock_guard<std::mutex> lock(m_tableMutex);
    m_frames[id] = std::make_shared<FrameState>();
  }
  send(Op::NEW_FRAME, [&](Writer &w) { w.pod(id); });
  return ANARIFrame(handleOf(id));
}

ANARIObject GuardDevice::newObject(const char *objectType, const char *type)
{
  Entry entry(this);
  ObjectId id = newId();
  send(Op::NEW_OBJECT, [&](Writer &w) {
    w.pod(id);
    w.string(objectType ? objectType : "");
    w.string(type ? type : "");
  });
  return handleOf(id);
}

// Arrays /////////////////////////////////////////////////////////////////////

static Op newArrayOp(int dims)
{
  return dims == 1 ? Op::NEW_ARRAY1D
      : dims == 2  ? Op::NEW_ARRAY2D
                   : Op::NEW_ARRAY3D;
}

static const char *newArrayCall(int dims)
{
  return dims == 1 ? "anariNewArray1D"
      : dims == 2  ? "anariNewArray2D"
                   : "anariNewArray3D";
}

ObjectId GuardDevice::newArray(int dims,
    const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userdata,
    ANARIDataType type,
    const uint64_t *n)
{
  ObjectId id = newId();

  ArrayState state;
  state.type = type;
  state.bytes = anari::sizeOf(type);
  for (int i = 0; i < dims; i++)
    state.bytes *= n[i];
  state.appMemory = appMemory;
  state.deleter = deleter;
  state.deleterUserData = userdata;
  const size_t bytes = state.bytes;

  // The contents of an array at or above the array ceiling cross in a region.
  // A dead device has no host to read one, so there it is made only where the
  // application needs the memory. If the system will not provide a region the
  // contents go inline. Below the ceiling they go inline in any case, and the
  // array is given a region when it is written again: see unmapArray() and
  // mapArray().
  //
  // An object array's region is not kept: it carries the ids of this one
  // message and a later unmap gets a region of its own. The host reads a
  // region when it comes to the message, which may be after the application
  // has moved on, so ids must not be rewritten where it has yet to read.
  const bool objects = anari::isObject(type);
  platform::Region idRegion;
  platform::Region &region = objects ? idRegion : state.region;
  const bool inRegion = bytes >= arrayCeiling() && !(m_dead && appMemory)
      && !(objects && !appMemory) && newRegion(bytes, region);
  if (!appMemory && (!inRegion || objects))
    state.owned.resize(bytes);
  // What the host is sent for each element of an object array, from now on.
  // An array with no contents yet is all NULL in the host.
  if (objects)
    state.sent.resize(bytes / sizeof(ObjectId), NULL_ID);
  ObjectId *sent = state.sent.empty() ? nullptr : state.sent.data();
  if (inRegion && appMemory)
    copyElements(
        region.data(), appMemory, type, bytes, newArrayCall(dims), sent);

  auto describe = [&](Writer &w) {
    w.pod(id);
    w.pod(int32_t(type));
    for (int i = 0; i < dims; i++)
      w.pod(n[i]);
  };
  if (inRegion) {
    if (!m_dead) {
      std::lock_guard<std::mutex> lock(m_callMutex);
      sendWithDescriptor(
          newArrayOp(dims),
          [&](Writer &w) {
            describe(w);
            w.pod(Storage::REGION);
            w.pod(uint64_t(0));
            w.pod(uint64_t(bytes));
          },
          region.descriptor());
    }
    region.closeDescriptor();
  } else {
    if (appMemory && !objects) {
      std::lock_guard<std::mutex> lock(m_callMutex);
      sendWithPayload(
          newArrayOp(dims),
          [&](Writer &w) {
            describe(w);
            w.pod(Storage::INLINE);
          },
          appMemory,
          bytes);
      sendIfFull();
    } else {
      send(newArrayOp(dims), [&](Writer &w) {
        describe(w);
        w.pod(appMemory ? Storage::INLINE : Storage::NONE);
        if (appMemory)
          writeElements(w, appMemory, type, bytes, newArrayCall(dims), sent);
      });
    }
  }

  std::lock_guard<std::mutex> lock(m_tableMutex);
  m_arrays[id] = std::move(state);
  return id;
}

ANARIArray1D GuardDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userdata,
    ANARIDataType type,
    uint64_t numItems1)
{
  Entry entry(this);
  const uint64_t n[] = {numItems1};
  return ANARIArray1D(
      handleOf(newArray(1, appMemory, deleter, userdata, type, n)));
}

ANARIArray2D GuardDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userdata,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  Entry entry(this);
  const uint64_t n[] = {numItems1, numItems2};
  return ANARIArray2D(
      handleOf(newArray(2, appMemory, deleter, userdata, type, n)));
}

ANARIArray3D GuardDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userdata,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  Entry entry(this);
  const uint64_t n[] = {numItems1, numItems2, numItems3};
  return ANARIArray3D(
      handleOf(newArray(3, appMemory, deleter, userdata, type, n)));
}

void *GuardDevice::mapArray(ANARIArray array)
{
  Entry entry(this);
  ArrayState *state = nullptr; // only the array's own calls touch it
  {
    std::lock_guard<std::mutex> lock(m_tableMutex);
    auto it = m_arrays.find(idOf(array));
    if (it != m_arrays.end()) {
      auto &a = it->second;
      // A managed array whose contents have crossed inline once is about to
      // be written again: from here on it lives in a region.
      const bool promote = a.filled && !a.region && !a.owned.empty()
          && a.sent.empty() && !m_dead;
      if (!promote)
        return a.mapped();
      state = &a;
    }
  }
  if (!state) {
    refuse("anariMapArray", "the object is not an array of this device");
    return nullptr;
  }

  // The region starts with what the array held. The host is sent it at the
  // unmap. If the system will not provide one the array stays as it is.
  platform::Region fresh;
  if (newRegion(state->bytes, fresh))
    std::memcpy(fresh.data(), state->owned.data(), state->bytes);
  std::lock_guard<std::mutex> lock(m_tableMutex);
  if (fresh) {
    state->region = std::move(fresh);
    Buffer().swap(state->owned);
  }
  return state->mapped();
}

void GuardDevice::unmapArray(ANARIArray array)
{
  Entry entry(this);
  if (m_dead)
    return;
  const ObjectId id = idOf(array);
  const void *source = nullptr;
  void *region = nullptr;
  ANARIDataType type = ANARI_UNKNOWN;
  size_t bytes = 0;
  ObjectId *sent = nullptr; // the array's, which only its own calls touch
  ArrayState *state = nullptr; // likewise: no one else lets go of the array
  bool found = false;
  bool promote = false;
  {
    std::lock_guard<std::mutex> lock(m_tableMutex);
    auto it = m_arrays.find(id);
    if (it != m_arrays.end()) {
      auto &a = it->second;
      state = &a;
      source = a.source();
      region = a.region.data();
      sent = a.sent.empty() ? nullptr : a.sent.data();
      type = a.type;
      bytes = a.bytes;
      found = true;
      // A shared array whose contents crossed inline when it was made, and
      // which is now being written again.
      promote = a.appMemory && !a.region && !sent;
    }
  }
  if (!found) {
    refuse("anariUnmapArray", "the object is not an array of this device");
    return;
  }

  // An array written again is given a region, which it keeps. A shared array
  // gets it here; a managed array got it when it was mapped. Either way the
  // host has yet to be sent it, which is what the open descriptor says. If
  // the system will not provide one the contents go inline, as before.
  int announce = -1;
  {
    platform::Region fresh;
    if (promote)
      newRegion(bytes, fresh);
    std::lock_guard<std::mutex> lock(m_tableMutex);
    if (fresh)
      state->region = std::move(fresh);
    region = state->region.data();
    announce = state->region.descriptor();
  }

  // Now is the only time the application may have written, so now is when
  // the host's view catches up: by a copy into the region, by nothing at all
  // when the application wrote to the region itself, or by sending the bytes.
  if (region && source)
    copyElements(region, source, type, bytes, "anariUnmapArray");

  // An object array's ids. An element that is what the host was last sent
  // goes as "unchanged": the application may have released that handle
  // since. When large they cross in a region made for this one message (see
  // newArray()). Handles are turned into ids before the call channel is
  // taken.
  platform::Region idRegion;
  Buffer ids;
  if (sent) {
    void *to = nullptr;
    if (bytes >= arrayCeiling() && newRegion(bytes, idRegion))
      to = idRegion.data();
    else {
      ids.resize(bytes);
      to = ids.data();
    }
    copyElements(to, source, type, bytes, "anariUnmapArray", sent, true);
  }

  auto describe = [&](Writer &w) {
    w.pod(id);
    w.pod(int32_t(type));
    w.pod(uint64_t(bytes));
  };
  auto inRegion = [&](Writer &w) {
    describe(w);
    w.pod(Storage::REGION);
    w.pod(uint64_t(0));
    w.pod(uint64_t(bytes));
  };
  if (idRegion) {
    std::lock_guard<std::mutex> lock(m_callMutex);
    sendWithDescriptor(Op::UNMAP_ARRAY, inRegion, idRegion.descriptor());
  } else if (announce >= 0) {
    {
      std::lock_guard<std::mutex> lock(m_callMutex);
      sendWithDescriptor(Op::UNMAP_ARRAY, inRegion, announce);
    }
    std::lock_guard<std::mutex> lock(m_tableMutex);
    state->region.closeDescriptor();
  } else if (region) {
    send(Op::UNMAP_ARRAY, inRegion);
  } else {
    {
      std::lock_guard<std::mutex> lock(m_callMutex);
      sendWithPayload(
          Op::UNMAP_ARRAY,
          [&](Writer &w) {
            describe(w);
            w.pod(Storage::INLINE);
          },
          sent ? ids.data() : source,
          bytes);
      sendIfFull();
    }
    std::lock_guard<std::mutex> lock(m_tableMutex);
    state->filled = true;
  }
}

void *GuardDevice::mapParameterArray(ANARIObject object,
    const char *name,
    int dims,
    ANARIDataType type,
    const uint64_t *n,
    uint64_t *elementStride)
{
  ObjectId objectId = NULL_ID;
  if (!target(object, "anariMapParameterArray", objectId))
    return nullptr;

  ParameterMapping m;
  m.dims = dims;
  m.type = type;
  size_t bytes = anari::sizeOf(type);
  for (int i = 0; i < dims; i++) {
    m.n[i] = n[i];
    bytes *= n[i];
  }
  m.bytes = bytes;
  // At or above the array ceiling and not object handles: the application
  // writes into the region the host will read.
  if (bytes < arrayCeiling() || anari::isObject(type)
      || !newRegion(bytes, m.region))
    m.data.resize(bytes);
  if (elementStride)
    *elementStride = anari::sizeOf(type);

  std::lock_guard<std::mutex> lock(m_tableMutex);
  auto &slot = m_mappings[{objectId, name}];
  slot = std::move(m);
  return slot.region ? slot.region.data() : slot.data.data();
}

void *GuardDevice::mapParameterArray1D(ANARIObject object,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t *elementStride)
{
  Entry entry(this);
  const uint64_t n[] = {numElements1};
  return mapParameterArray(object, name, 1, dataType, n, elementStride);
}

void *GuardDevice::mapParameterArray2D(ANARIObject object,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t *elementStride)
{
  Entry entry(this);
  const uint64_t n[] = {numElements1, numElements2};
  return mapParameterArray(object, name, 2, dataType, n, elementStride);
}

void *GuardDevice::mapParameterArray3D(ANARIObject object,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3,
    uint64_t *elementStride)
{
  Entry entry(this);
  const uint64_t n[] = {numElements1, numElements2, numElements3};
  return mapParameterArray(object, name, 3, dataType, n, elementStride);
}

// To the host this is three ordinary calls: a new array holding the mapped
// contents, a parameter set to it, and the release of our reference.
void GuardDevice::unmapParameterArray(ANARIObject object, const char *name)
{
  Entry entry(this);
  ObjectId objectId = NULL_ID;
  if (!target(object, "anariUnmapParameterArray", objectId))
    return;
  ParameterMapping m;
  {
    std::lock_guard<std::mutex> lock(m_tableMutex);
    auto it = m_mappings.find({objectId, name});
    if (it == m_mappings.end())
      return;
    m = std::move(it->second);
    m_mappings.erase(it);
  }

  if (m_dead)
    return;

  // Object arrays at or above the ceiling get their region only now, filled
  // with ids.
  if (!m.region && m.bytes >= arrayCeiling() && newRegion(m.bytes, m.region))
    copyElements(m.region.data(),
        m.data.data(),
        m.type,
        m.bytes,
        "anariUnmapParameterArray");

  // Handles are turned into ids before the call channel is taken.
  if (!m.region && anari::isObject(m.type)) {
    Buffer ids(m.bytes);
    copyElements(
        ids.data(), m.data.data(), m.type, m.bytes, "anariUnmapParameterArray");
    m.data = std::move(ids);
  }

  const ObjectId arrayId = newInternalId();
  auto describe = [&](Writer &w) {
    w.pod(arrayId);
    w.pod(int32_t(m.type));
    for (int i = 0; i < m.dims; i++)
      w.pod(m.n[i]);
  };
  std::lock_guard<std::mutex> lock(m_callMutex);
  if (m.region) {
    sendWithDescriptor(
        newArrayOp(m.dims),
        [&](Writer &w) {
          describe(w);
          w.pod(Storage::REGION);
          w.pod(uint64_t(0));
          w.pod(uint64_t(m.bytes));
        },
        m.region.descriptor());
    if (m_dead)
      return;
  } else {
    sendWithPayload(
        newArrayOp(m.dims),
        [&](Writer &w) {
          describe(w);
          w.pod(Storage::INLINE);
        },
        m.data.data(),
        m.bytes);
    if (m_dead)
      return;
  }
  {
    Writer w(m_out, Op::SET_PARAMETER);
    w.pod(objectId);
    w.string(name);
    w.pod(int32_t(ANARI_ARRAY1D + (m.dims - 1)));
    w.pod(arrayId);
  }
  {
    Writer w(m_out, Op::RELEASE);
    w.pod(arrayId);
  }
  sendIfFull();
}

// Parameters /////////////////////////////////////////////////////////////////

void GuardDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  Entry entry(this);
  ObjectId id = NULL_ID;
  if (!target(object, "anariSetParameter", id))
    return;
  const std::string param = name;

  // Callbacks and their user data stay in the adapter; the host never sees
  // them. They take effect at the next commit.
  if (id == DEVICE_ID && param == "statusCallback"
      && type == ANARI_STATUS_CALLBACK) {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_pendingStatusCallback =
        mem ? *static_cast<const ANARIStatusCallback *>(mem) : nullptr;
    return;
  }
  if (id == DEVICE_ID && param == "statusCallbackUserData"
      && type == ANARI_VOID_POINTER) {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_pendingStatusUserData = mem;
    return;
  }
  if (param == "frameCompletionCallback"
      || param == "frameCompletionCallbackUserData") {
    if (auto frame = frameState(id)) {
      std::lock_guard<std::mutex> lock(frame->mutex);
      if (type == ANARI_FRAME_COMPLETION_CALLBACK) {
        frame->pendingCallback = mem
            ? *static_cast<const ANARIFrameCompletionCallback *>(mem)
            : nullptr;
        return;
      } else if (type == ANARI_VOID_POINTER) {
        frame->pendingUserData = mem;
        return;
      }
    }
  }

  // What cannot cross to another process is refused here.
  const bool isList = type == ANARI_STRING_LIST;
  const bool sendable = type == ANARI_STRING || isList || anari::isObject(type)
      || (anari::sizeOf(type) > 0 && type != ANARI_VOID_POINTER
          && type != ANARI_DEVICE && type != ANARI_LIBRARY
          && type != ANARI_STATUS_CALLBACK && type != ANARI_MEMORY_DELETER
          && type != ANARI_FRAME_COMPLETION_CALLBACK
          && type != ANARI_DATA_TYPE_LIST && type != ANARI_PARAMETER_LIST);
  if (!sendable || !mem) {
    report(id,
        ANARI_SEVERITY_ERROR,
        ANARI_STATUS_INVALID_ARGUMENT,
        "parameter '" + param + "' of type " + anari::toString(type)
            + " cannot be passed to the wrapped device");
    return;
  }

  ObjectId value = NULL_ID;
  if (anari::isObject(type)) {
    const ANARIObject handle = *static_cast<const ANARIObject *>(mem);
    if (handle && !target(handle, "anariSetParameter", value))
      return;
  }

  send(Op::SET_PARAMETER, [&](Writer &w) {
    w.pod(id);
    w.string(param);
    w.pod(int32_t(type));
    if (anari::isObject(type))
      w.pod(value);
    else if (type == ANARI_STRING)
      w.string(static_cast<const char *>(mem));
    else if (isList) {
      auto *list = static_cast<const char *const *>(mem);
      uint32_t count = 0;
      while (list[count])
        count++;
      w.pod(count);
      for (uint32_t i = 0; i < count; i++)
        w.string(list[i]);
    } else
      w.bytes(mem, anari::sizeOf(type));
  });
}

static bool keptInAdapter(const std::string &name)
{
  return name == "statusCallback" || name == "statusCallbackUserData"
      || name == "frameCompletionCallback"
      || name == "frameCompletionCallbackUserData";
}

void GuardDevice::unsetParameter(ANARIObject object, const char *name)
{
  Entry entry(this);
  ObjectId id = NULL_ID;
  if (!target(object, "anariUnsetParameter", id))
    return;
  const std::string param = name;

  if (keptInAdapter(param)) {
    if (id == DEVICE_ID) {
      std::lock_guard<std::mutex> lock(m_statusMutex);
      if (param == "statusCallback")
        m_pendingStatusCallback = nullptr;
      else if (param == "statusCallbackUserData")
        m_pendingStatusUserData = nullptr;
    } else if (auto frame = frameState(id)) {
      std::lock_guard<std::mutex> lock(frame->mutex);
      if (param == "frameCompletionCallback")
        frame->pendingCallback = nullptr;
      else if (param == "frameCompletionCallbackUserData")
        frame->pendingUserData = nullptr;
    }
    return;
  }

  send(Op::UNSET_PARAMETER, [&](Writer &w) {
    w.pod(id);
    w.string(param);
  });
}

void GuardDevice::unsetAllParameters(ANARIObject object)
{
  Entry entry(this);
  ObjectId id = NULL_ID;
  if (!target(object, "anariUnsetAllParameters", id))
    return;
  if (id == DEVICE_ID) {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_pendingStatusCallback = nullptr;
    m_pendingStatusUserData = nullptr;
  } else if (auto frame = frameState(id)) {
    std::lock_guard<std::mutex> lock(frame->mutex);
    frame->pendingCallback = nullptr;
    frame->pendingUserData = nullptr;
  }
  send(Op::UNSET_ALL_PARAMETERS, [&](Writer &w) { w.pod(id); });
}

void GuardDevice::commitParameters(ANARIObject object)
{
  Entry entry(this);
  ObjectId id = NULL_ID;
  if (!target(object, "anariCommitParameters", id))
    return;
  if (id == DEVICE_ID) {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusCommitted = true;
    m_statusCallback = m_pendingStatusCallback;
    m_statusUserData = m_pendingStatusUserData;
  } else if (auto frame = frameState(id)) {
    std::lock_guard<std::mutex> lock(frame->mutex);
    frame->callback = frame->pendingCallback;
    frame->userData = frame->pendingUserData;
  }
  send(Op::COMMIT_PARAMETERS, [&](Writer &w) { w.pod(id); });
}

// Lifetime ///////////////////////////////////////////////////////////////////

void GuardDevice::retain(ANARIObject object)
{
  Entry entry(this);
  const ObjectId id = idOf(object);
  if (id == DEVICE_ID) {
    m_references++;
    return;
  }
  if (id == NULL_ID)
    return;
  bool held = false;
  {
    std::lock_guard<std::mutex> lock(m_tableMutex);
    auto live = m_live.find(id);
    if (live != m_live.end()) {
      live->second++;
      held = true;
    }
  }
  if (!held) {
    refuse("anariRetain", NOT_A_HANDLE);
    return;
  }
  send(Op::RETAIN, [&](Writer &w) { w.pod(id); });
}

// The application lets go of one of its references to the device.
void GuardDevice::releaseReference()
{
  if (--m_references != 0) {
    Entry entry(this);
    return;
  }

  const auto self = std::this_thread::get_id();
  if (self == m_dispatcherId) {
    // Inside a callback: the dispatcher cannot wait for itself, so it
    // finishes the release once the callback has returned. A death the
    // application has not heard of is told now, as by any call.
    {
      Entry entry(this);
    }
    m_releasedInCallback = true;
    auto &releases = releasesInCallbacks();
    std::lock_guard<std::mutex> lock(releases.mutex);
    releases.unfinished.push_back(self);
    return;
  }
  if (m_deliveringThread.load() == self) {
    // Inside the death message's callback, under a call that is still using
    // the device: that call finishes the release on its way out.
    m_releasedInDelivery = true;
    return;
  }
  releaseDevice();
}

void GuardDevice::release(ANARIObject object)
{
  const ObjectId id = idOf(object);
  if (id == DEVICE_ID) {
    releaseReference();
    return;
  }
  Entry entry(this);
  if (id == NULL_ID)
    return;

  // The application's last release of an array is when its deleter is owed.
  ANARIMemoryDeleter deleter = nullptr;
  const void *deleterUserData = nullptr;
  const void *appMemory = nullptr;
  std::shared_ptr<FrameState> releasedFrame;
  bool held = false;
  {
    std::lock_guard<std::mutex> lock(m_tableMutex);
    auto live = m_live.find(id);
    held = live != m_live.end();
    if (held && --live->second == 0) {
      m_live.erase(live);
      auto a = m_arrays.find(id);
      if (a != m_arrays.end()) {
        deleter = a->second.deleter;
        deleterUserData = a->second.deleterUserData;
        appMemory = a->second.appMemory;
        m_arrays.erase(a);
      }
      auto f = m_frames.find(id);
      if (f != m_frames.end()) {
        releasedFrame = std::move(f->second);
        m_frames.erase(f);
      }
    }
  }
  if (!held) {
    refuse("anariRelease", NOT_A_HANDLE);
    return;
  }

  // What the application had mapped goes now, not whenever the last queued
  // event for the frame does.
  if (releasedFrame) {
    std::lock_guard<std::mutex> lock(releasedFrame->mutex);
    releasedFrame->mapped.clear();
    releasedFrame->regions.clear();
    releasedFrame->released = true;
    releasedFrame->changed.notify_all();
  }

  // Sent before the deleter runs: the host already holds its own copy.
  send(Op::RELEASE, [&](Writer &w) { w.pod(id); });

  if (deleter)
    deleter(deleterUserData, appMemory);
}

// Queries ////////////////////////////////////////////////////////////////////

int GuardDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask)
{
  Entry entry(this);
  ObjectId id = NULL_ID;
  if (!target(object, "anariGetProperty", id))
    return 0;

  // The adapter's own counters, which no message is sent for: they are the
  // same on a dead device.
  uint64_t count = 0;
  if (id == DEVICE_ID && name && counter(name, count)) {
    if (type != ANARI_UINT64 || size < sizeof(count))
      return 0;
    std::memcpy(mem, &count, sizeof(count));
    return 1;
  }

  Buffer reply;
  auto fill = [&](Writer &w) {
    w.pod(id);
    w.string(name);
    w.pod(int32_t(type));
    w.pod(uint64_t(size));
    w.pod(int32_t(mask));
  };
  if (!call(Op::GET_PROPERTY, fill, reply))
    return 0;

  Reader r(reply);
  int found = r.pod<int32_t>();
  if (type == ANARI_STRING_LIST) {
    const void *list = intern(r, std::strcmp(name, "extension") == 0);
    if (found && list && size >= sizeof(list))
      std::memcpy(mem, &list, sizeof(list));
    else
      found = 0;
  } else {
    const uint8_t *value = r.bytes(size);
    if (found && value)
      std::memcpy(mem, value, size);
    else
      found = 0;
  }
  return found;
}

const char **GuardDevice::getObjectSubtypes(ANARIDataType objectType)
{
  Entry entry(this);
  const void *answer = introspect(
      Op::GET_OBJECT_SUBTYPES, [&](Writer &w) { w.pod(int32_t(objectType)); });
  return static_cast<const char **>(const_cast<void *>(answer));
}

const void *GuardDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  Entry entry(this);
  const bool extensionList = infoName && std::strcmp(infoName, "extension") == 0
      && infoType == ANARI_STRING_LIST;
  return introspect(
      Op::GET_OBJECT_INFO,
      [&](Writer &w) {
        w.pod(int32_t(objectType));
        w.string(objectSubtype);
        w.string(infoName);
        w.pod(int32_t(infoType));
      },
      extensionList);
}

const void *GuardDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  Entry entry(this);
  return introspect(Op::GET_PARAMETER_INFO, [&](Writer &w) {
    w.pod(int32_t(objectType));
    w.string(objectSubtype);
    w.string(parameterName);
    w.pod(int32_t(parameterType));
    w.string(infoName);
    w.pod(int32_t(infoType));
  });
}

// Frames /////////////////////////////////////////////////////////////////////

const void *GuardDevice::frameBufferMap(ANARIFrame fb,
    const char *channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  Entry entry(this);
  const ObjectId id = idOf(fb);
  auto frame = frameFor(fb, "anariMapFrame");

  // Device memory is no use in this process, and the host could not copy it.
  if (!frame || isOutOfScopeChannel(channel)) {
    if (frame) {
      report(id,
          ANARI_SEVERITY_ERROR,
          ANARI_STATUS_INVALID_ARGUMENT,
          std::string("frame channel '") + channel
              + "' cannot be mapped through the guard device");
    }
    if (width)
      *width = 0;
    if (height)
      *height = 0;
    if (pixelType)
      *pixelType = ANARI_UNKNOWN;
    return nullptr;
  }

  Buffer reply;
  int descriptor = -1;
  auto fill = [&](Writer &w) {
    w.pod(id);
    w.string(channel);
  };
  // On a dead device the empty reply reads as no pixels of no type.
  call(Op::FRAME_BUFFER_MAP, fill, reply, &descriptor);

  Reader r(reply);
  uint32_t w = r.pod<uint32_t>();
  uint32_t h = r.pod<uint32_t>();
  auto type = ANARIDataType(r.pod<int32_t>());
  const Storage storage = r.pod<Storage>();

  // Either way the pixels end up in memory the adapter holds, which the
  // application can go on reading whatever becomes of the host.
  const void *pixels = nullptr;
  if (frame && storage == Storage::INLINE) {
    const uint64_t bytes = r.pod<uint64_t>();
    const uint8_t *data = r.bytes(bytes);
    std::lock_guard<std::mutex> lock(frame->mutex);
    frame->regions.erase(channel);
    if (data && bytes > 0) {
      auto &copy = frame->mapped[channel];
      copy.assign(data, data + bytes);
      pixels = copy.data();
    }
  } else if (frame && storage == Storage::REGION) {
    const uint64_t offset = r.pod<uint64_t>();
    const uint64_t length = r.pod<uint64_t>();
    std::lock_guard<std::mutex> lock(frame->mutex);
    frame->mapped.erase(channel);
    // A descriptor means the host made a new region for this channel, which
    // replaces the one from an earlier mapping.
    if (descriptor >= 0) {
      platform::Region fresh;
      if (platform::Region::map(descriptor, false, fresh)) {
        m_regionsCreated++; // by the host
        frame->regions[channel] = std::move(fresh);
      } else
        frame->regions.erase(channel);
    }
    auto it = frame->regions.find(channel);
    if (r.ok() && it != frame->regions.end() && offset <= it->second.size()
        && length <= it->second.size() - offset)
      pixels = static_cast<const uint8_t *>(it->second.data()) + offset;
  }
  platform::closeDescriptor(descriptor);

  // Pixels the host has but the adapter cannot reach: nothing the host says
  // about this frame can be relied on from here.
  if (storage == Storage::REGION && frame && !pixels) {
    markDead();
    w = h = 0;
    type = ANARI_UNKNOWN;
  }

  if (width)
    *width = w;
  if (height)
    *height = h;
  if (pixelType)
    *pixelType = type;
  return pixels;
}

void GuardDevice::frameBufferUnmap(ANARIFrame fb, const char *channel)
{
  Entry entry(this);
  // Nothing to tell the host: it ended its own mapping when it replied. A
  // region stays, for the next mapping of the channel to reuse.
  if (auto frame = frameState(idOf(fb))) {
    std::lock_guard<std::mutex> lock(frame->mutex);
    frame->mapped.erase(channel);
  }
}

void GuardDevice::renderFrame(ANARIFrame f)
{
  Entry entry(this);
  const ObjectId id = idOf(f);
  auto frame = frameFor(f, "anariRenderFrame");
  if (!frame)
    return;
  {
    // A render asked of a dead device is never in flight, so nothing waits
    // on it and its completion callback does not run.
    std::lock_guard<std::mutex> lock(frame->mutex);
    if (m_dead)
      return;
    frame->renders++;
    frame->hostRendering = true;
    frame->inFlight = true;
    frame->calledBack = false;
  }
  send(Op::RENDER_FRAME, [&](Writer &w) { w.pod(id); }, true);
}

int GuardDevice::frameReady(ANARIFrame f, ANARIWaitMask mask)
{
  Entry entry(this);
  auto frame = frameFor(f, "anariFrameReady");
  if (!frame || m_dead)
    return 1;

  // Answered from the frame's own state; no message. A caller on the
  // dispatcher cannot wait for the dispatcher, so it waits for the host.
  // Device death ends the wait: on a dead device every frame is ready. So
  // does the frame's release by another thread, which leaves nothing to wait
  // for; the waiter holds the frame's state, and reads no more of it than
  // that it was released.
  const bool onDispatcher = std::this_thread::get_id() == m_dispatcherId;
  auto ready = [&] {
    return m_dead || frame->released
        || (onDispatcher ? !frame->hostRendering : !frame->inFlight);
  };

  std::unique_lock<std::mutex> lock(frame->mutex);
  if (mask == ANARI_WAIT)
    frame->changed.wait(lock, ready);
  return ready() ? 1 : 0;
}

void GuardDevice::discardFrame(ANARIFrame f)
{
  Entry entry(this);
  if (!frameFor(f, "anariDiscardFrame"))
    return;
  send(Op::DISCARD_FRAME, [&](Writer &w) { w.pod(idOf(f)); }, true);
}

// Events /////////////////////////////////////////////////////////////////////

void GuardDevice::post(Event &&e)
{
  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.push_back(std::move(e));
  }
  m_queueChanged.notify_one();
}

void GuardDevice::report(ObjectId source,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const std::string &message)
{
  if (m_dead)
    return; // calls on a dead device report nothing
  Event e;
  e.id = source;
  e.sourceType = source == DEVICE_ID ? ANARI_DEVICE : ANARI_OBJECT;
  e.severity = severity;
  e.code = code;
  e.message = message;
  post(std::move(e));
}

// The reader only moves events from the channel to the queue. It never calls
// the application, so it never stops draining.
void GuardDevice::readEvents()
{
  MessageHeader header;
  Buffer payload;
  bool hostSeenEnded = false;
  bool deviceExists = false;
  for (;;) {
    auto wake = platform::waitReadableOrHostEnd(m_host.event, m_host.process);
    hostSeenEnded = wake == platform::Wake::HOST_ENDED;
    if (wake != platform::Wake::READABLE
        || !receive(m_host.event, header, payload))
      break;

    if (Op(header.opcode) == Op::DEVICE_CREATED_EVENT) {
      deviceExists = true;
      continue;
    }

    Reader r(payload);
    Event e;
    e.kind = Op(header.opcode);
    e.beforeDevice = !deviceExists;
    e.id = r.pod<ObjectId>();
    if (e.kind == Op::STATUS_EVENT) {
      e.sourceType = ANARIDataType(r.pod<int32_t>());
      e.severity = ANARIStatusSeverity(r.pod<int32_t>());
      e.code = ANARIStatusCode(r.pod<int32_t>());
      e.message = r.string();
    } else if (e.kind == Op::FRAME_COMPLETED_EVENT) {
      e.render = r.pod<uint64_t>();
      e.frame = frameState(e.id);
      if (e.frame) {
        // A completion for anything but the latest render releases nobody.
        std::lock_guard<std::mutex> lock(e.frame->mutex);
        if (e.render != e.frame->renders)
          continue;
        e.frame->hostRendering = false;
        e.frame->changed.notify_all();
      }
    } else if (e.kind == Op::FRAME_CALLED_BACK_EVENT) {
      e.frame = frameState(e.id);
      if (e.frame) {
        std::lock_guard<std::mutex> lock(e.frame->mutex);
        e.frame->calledBack = true;
      }
    }
    // Either kind of frame event for a frame the application has released
    // has no frame here, and the dispatcher drops it.
    post(std::move(e));
  }

  // The event channel has ended. That is expected when creation fails and
  // during a release, which then looks after a death of its own; anywhere
  // else the host is gone.
  {
    std::unique_lock<std::mutex> lock(m_creationMutex);
    m_creationResolved.wait(lock, [&] { return m_created || m_abandoned; });
    if (m_abandoned)
      return;
  }
  if (m_releasing)
    return;
  markDead(hostSeenEnded);
  announceDeath();
}

// The dispatcher calls the application's frame-completion callbacks and its
// status callback for everything but the death message, and holds no adapter
// lock while it does.
void GuardDevice::dispatchEvents()
{
  {
    std::lock_guard<std::mutex> lock(m_queueMutex); // m_dispatcherId is set
  }
  for (;;) {
    Event e;
    {
      std::unique_lock<std::mutex> lock(m_queueMutex);
      m_queueChanged.wait(lock, [&] { return !m_queue.empty(); });
      e = std::move(m_queue.front());
      m_queue.pop_front();
    }
    if (e.stop)
      return;

    if (e.death) {
      // Everything from before the death has been delivered, so a call may
      // now deliver the death message. If the application is inside a call
      // it does so on its way out, and what follows here (the callbacks of
      // frames in flight) waits for that; an application that is not hears
      // of its frames at once and of the death at its next call.
      std::unique_lock<std::mutex> lock(m_deathMutex);
      m_drained = true;
      m_deathChanged.notify_all();
      m_deathChanged.wait(lock, [&] { return m_delivered || m_callers == 0; });
      continue;
    }

    if (e.kind == Op::STATUS_EVENT && e.beforeDevice) {
      // From the library, as far as the application can tell: it holds no
      // handle to this device yet, and if creation fails it never will.
      if (auto callback = defaultStatusCallback()) {
        callback(defaultStatusCallbackUserPtr(),
            nullptr,
            nullptr,
            ANARI_LIBRARY,
            e.severity,
            e.code,
            e.message.c_str());
      }
    } else if (e.kind == Op::STATUS_EVENT) {
      ANARIStatusCallback callback = nullptr;
      const void *userData = nullptr;
      {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        if (m_statusCommitted && m_statusCallback) {
          callback = m_statusCallback;
          userData = m_statusUserData;
        } else {
          callback = defaultStatusCallback();
          userData = defaultStatusCallbackUserPtr();
        }
      }
      if (callback) {
        callback(userData,
            this_device(),
            handleOf(e.id),
            e.sourceType,
            e.severity,
            e.code,
            e.message.c_str());
      }
    } else if (e.kind == Op::FRAME_COMPLETED_EVENT && e.frame) {
      // The application has asked for another render since: still in flight.
      std::lock_guard<std::mutex> lock(e.frame->mutex);
      if (e.render == e.frame->renders) {
        e.frame->inFlight = false;
        e.frame->changed.notify_all();
      }
    } else if (e.kind == Op::FRAME_CALLED_BACK_EVENT && e.frame) {
      // Once for each time the wrapped device called back, so a render it
      // declined runs no callback here either.
      ANARIFrameCompletionCallback callback = nullptr;
      const void *userData = nullptr;
      {
        std::lock_guard<std::mutex> lock(e.frame->mutex);
        callback = e.frame->callback;
        userData = e.frame->userData;
      }
      if (callback)
        callback(userData, this_device(), ANARIFrame(handleOf(e.id)));
    }

    if (m_releasedInCallback) {
      finishReleaseOnDispatcher();
      return; // nothing of the device is left
    }
  }
}

} // namespace anari_guard
