// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "host/Host.h"
#include "common/Tuning.h"
#include "common/Wire.h"

// anari
#include <anari/anari.h>
#include <anari/anari_cpp/Traits.h>
#include <anari/frontend/type_utility.h>
// helium
#include <helium/TaskQueue.h>
// std
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace anari_guard {

namespace {

// A message the host cannot make sense of is a bug in the guard device: say
// so and end, which the adapter sees as device death.
// Either thread may get here while the other is busy, so nothing is torn down.
[[noreturn]] void protocolError(const char *what)
{
  std::fprintf(stderr, "[anariGuardHost] protocol error: %s\n", what);
  std::_Exit(1);
}

// A failure the host cannot carry on from, which is not the adapter's doing.
[[noreturn]] void fatal(const char *what)
{
  std::fprintf(stderr, "[anariGuardHost] %s\n", what);
  std::_Exit(1);
}

// The memory behind one array of the wrapped device, which is always a shared
// array: the adapter's region itself, or host memory when the contents came
// inline or are object handles translated from the ids the adapter sent.
// An array over host memory may be given a region later, when the application
// writes it again: the wrapped device goes on reading host memory, and the
// region's contents are copied into it at each unmap.
struct ArrayBacking
{
  platform::Region region;
  const uint8_t *contents{nullptr}; // within the region, read in place
  const uint8_t *staged{nullptr}; // within the region, copied to 'heap'
  void *heap{nullptr};

  ~ArrayBacking()
  {
    std::free(heap);
  }
};

// The wrapped device's deleter for every array: it may hold an array longer
// than the application does, so this, not the release, is when the host lets
// go of the region.
void freeArrayBacking(const void *userPtr, const void * /*appMemory*/)
{
  delete static_cast<ArrayBacking *>(const_cast<void *>(userPtr));
}

// The reader stops taking messages off the call channel while more than this
// many bytes of them wait for the worker, so the channel pushes back on the
// adapter.
constexpr size_t QUEUED_BYTES_LIMIT = 64 * 1024 * 1024;

// The host is two threads. The reader (the thread that called runHost) takes
// messages off the call channel and queues one task for each. The worker
// executes them in order and is the only thread that calls the wrapped device,
// which the ANARI specification requires; it also sends every reply. Whether
// a render is over is found by tasks of the same queue, the probes.

struct Host
{
  Host(int call, int event, CallObserver *observer)
      : m_call(call), m_event(event), m_observer(observer)
  {}

  int run();

 private:
  struct Entry
  {
    ANARIObject handle{nullptr}; // null: creation failed in the wrapped device
    uint32_t count{1};
  };

  // Kept for good: the wrapped device may call back for a frame long after
  // the application released it. All but 'host' and 'id' are the worker's.
  struct FrameCookie
  {
    Host *host;
    ObjectId id;
    ANARIFrame frame{nullptr}; // null: creation failed, or released
    uint64_t rendered{0}; // how many renders were asked for
    uint64_t completed{0}; // how many of them a completion accounts for
  };

  // One message off the call channel, on its way to the worker
  struct Message
  {
    MessageHeader header;
    Buffer payload;
    int descriptor{-1};
  };

  // First message ////////////////////////////////////////////////////////////

  int first(const Message &m);
  bool createDevice(Reader &r);
  void queryExtensions(Reader &r);

  // Reader ///////////////////////////////////////////////////////////////////

  void waitForRoom();
  void postProbe();

  // Worker ///////////////////////////////////////////////////////////////////

  void execute(Message &m);
  void probe();
  void probeFrame(FrameCookie &cookie);
  void complete(FrameCookie &cookie);

  // Calls ////////////////////////////////////////////////////////////////////

  void dispatch(Op op, Reader &r);
  void newArray(int dims, Reader &r);
  void unmapArray(Reader &r);
  void setParameter(Reader &r);
  void getProperty(Reader &r);
  void frameBufferMap(Reader &r);
  void renderFrame(Reader &r);
  void release(Reader &r);

  // Object table /////////////////////////////////////////////////////////////

  void add(ObjectId id, ANARIObject handle);
  ANARIObject resolve(ObjectId id);
  void installCompletionCallback(ObjectId id, ANARIObject frame);
  void fillElements(void *dst,
      const uint8_t *src,
      ANARIDataType type,
      size_t bytes,
      bool keepUnchanged = false);

  // Replies and events ///////////////////////////////////////////////////////

  void writeValue(Writer &w, ANARIDataType type, const void *p);
  void writeStringList(Writer &w, const char *const *list);
  void sendEvent(const Buffer &b);
  void sendFrameEvent(Op kind, ObjectId id, const uint64_t *render = nullptr);

  static void statusCallback(const void *userData,
      ANARIDevice,
      ANARIObject source,
      ANARIDataType sourceType,
      ANARIStatusSeverity severity,
      ANARIStatusCode code,
      const char *message);
  static void frameCompleted(const void *userData, ANARIDevice, ANARIFrame);

  // Take the descriptor that came with the message being dispatched.
  int takeDescriptor();

  int m_call;
  int m_event;
  CallObserver *m_observer;

  // The descriptor that arrived with the current message, and the one to
  // send with its reply; -1 for none.
  int m_descriptor{-1};
  int m_replyDescriptor{-1};

  ANARILibrary m_library{nullptr};
  ANARIDevice m_device{nullptr};
  bool m_hasCompletionCallback{false};

  std::unordered_map<ObjectId, Entry> m_objects;
  std::unordered_map<ObjectId, std::unique_ptr<FrameCookie>> m_frames;
  // Frames with a render no completion accounts for yet
  std::vector<FrameCookie *> m_rendering;
  // Owned by the wrapped arrays' deleter; listed while the host holds a
  // reference to the array, which keeps the deleter from running.
  std::unordered_map<ObjectId, ArrayBacking *> m_arrays;
  // The region each mapped channel of a frame was last copied into
  std::map<std::pair<ObjectId, std::string>, platform::Region> m_frameRegions;

  // Wrapped handle back to object id, read from status callbacks on any thread
  std::mutex m_reverseMutex;
  std::unordered_map<ANARIObject, ObjectId> m_reverse;

  std::mutex m_eventMutex;

  Buffer m_reply;
  bool m_hasReply{false};

  // Bytes of messages queued and not yet executed
  std::mutex m_queuedMutex;
  std::condition_variable m_queuedChanged;
  size_t m_queuedBytes{0};

  // Render messages read, and how many of those a completion accounts for:
  // while they differ a render is outstanding. The first is the reader's.
  uint64_t m_rendersRead{0};
  std::atomic<uint64_t> m_rendersSettled{0};
  // A probe is queued and has not started
  std::atomic<bool> m_probePosted{false};

  // Last, so that the worker is gone before anything it uses
  helium::tasking::TaskQueue m_worker{64};
};

// Main loop //////////////////////////////////////////////////////////////////

int Host::run()
{
  {
    Buffer hello;
    {
      Writer w(hello, Op::HELLO);
      w.pod(PROTOCOL_VERSION);
    }
    if (!platform::sendAll(m_call, hello.data(), hello.size()))
      return 1;
  }

  // Exactly one first message, of one of two kinds.
  {
    Message m;
    if (!recvMessage(m_call, m.header, m.payload))
      std::_Exit(0);
    int result = -1;
    m_worker.enqueue([&] { result = first(m); }).wait();
    if (result >= 0)
      return result;
  }

  // When the reader last asked for a probe
  auto probed = std::chrono::steady_clock::now();

  for (;;) {
    waitForRoom();

    // While a render is outstanding the worker has to keep asking whether it
    // is over. The adapter may have nothing more to say until it is, and it
    // may as well never stop talking: a probe is due one period after the
    // last, whether or not messages keep arriving.
    if (m_rendersRead != m_rendersSettled) {
      using std::chrono::microseconds;
      const auto period = microseconds(probePeriodMicroseconds());
      auto left = period
          - std::chrono::duration_cast<microseconds>(
              std::chrono::steady_clock::now() - probed);
      if (left.count() <= 0) {
        postProbe();
        probed = std::chrono::steady_clock::now();
        left = period;
      }
      if (!platform::waitReadable(m_call, uint64_t(left.count()))) {
        postProbe();
        probed = std::chrono::steady_clock::now();
        continue;
      }
    }

    // End-of-file: the application went away without releasing the device.
    // The host ends here and now, whatever the worker is in the middle of.
    auto m = std::make_shared<Message>();
    if (!recvMessage(m_call, m->header, m->payload, m->descriptor))
      std::_Exit(0);

    const Op op = Op(m->header.opcode);
    if (op == Op::RENDER_FRAME) {
      // The render's own task asks first; the probes follow a period later.
      if (m_rendersRead == m_rendersSettled)
        probed = std::chrono::steady_clock::now();
      m_rendersRead++;
    }
    {
      std::lock_guard<std::mutex> lock(m_queuedMutex);
      m_queuedBytes += sizeof(m->header) + m->payload.size();
    }
    auto done = m_worker.enqueue([this, m] { execute(*m); });

    if (op == Op::RELEASE_DEVICE) {
      done.wait();
      platform::closeEndpoint(m_event);
      return 0;
    }
  }
}

// A reader stopped here does not see end-of-file until the worker catches up.
void Host::waitForRoom()
{
  std::unique_lock<std::mutex> lock(m_queuedMutex);
  m_queuedChanged.wait(
      lock, [&] { return m_queuedBytes <= QUEUED_BYTES_LIMIT; });
}

// From the reader, or from whichever thread the wrapped device calls back on.
void Host::postProbe()
{
  if (m_probePosted.exchange(true))
    return; // the one already queued will see whatever this one would
  m_worker.enqueue([this] {
    m_probePosted = false;
    probe();
  });
}

// Worker /////////////////////////////////////////////////////////////////////

void Host::execute(Message &m)
{
  const Op op = Op(m.header.opcode);
  Reader r(m.payload);
  m_descriptor = m.descriptor;
  m_reply.clear();
  m_hasReply = false;

  if (m_observer)
    m_observer->before(op);
  dispatch(op, r);
  if (!r.ok())
    protocolError("truncated message");
  if (m_descriptor >= 0)
    protocolError("descriptor sent with a message that takes none");
  if (m_observer)
    m_observer->after(op);

  if (m_hasReply) {
    const bool sent = m_replyDescriptor < 0
        ? platform::sendAll(m_call, m_reply.data(), m_reply.size())
        : platform::sendWithDescriptor(
              m_call, m_reply.data(), m_reply.size(), m_replyDescriptor);
    platform::closeDescriptor(m_replyDescriptor);
    m_replyDescriptor = -1;
    if (!sent)
      std::_Exit(0);
  }

  {
    std::lock_guard<std::mutex> lock(m_queuedMutex);
    m_queuedBytes -= sizeof(m.header) + m.payload.size();
  }
  m_queuedChanged.notify_one();
}

// Ask the wrapped device about every frame with a render outstanding.
void Host::probe()
{
  if (!m_device)
    return; // released
  const auto rendering = m_rendering; // complete() takes frames out
  for (FrameCookie *cookie : rendering)
    probeFrame(*cookie);
}

void Host::probeFrame(FrameCookie &cookie)
{
  if (cookie.frame && cookie.rendered != cookie.completed
      && anariFrameReady(m_device, cookie.frame, ANARI_NO_WAIT))
    complete(cookie);
}

// Every render asked of this frame so far is over, or never will be.
void Host::complete(FrameCookie &cookie)
{
  if (cookie.rendered == cookie.completed)
    return;
  // For a frame the application has released the adapter would drop it.
  if (m_objects.count(cookie.id))
    sendFrameEvent(Op::FRAME_COMPLETED_EVENT, cookie.id, &cookie.rendered);
  m_rendersSettled += cookie.rendered - cookie.completed;
  cookie.completed = cookie.rendered;
  m_rendering.erase(
      std::remove(m_rendering.begin(), m_rendering.end(), &cookie),
      m_rendering.end());
}

// First message //////////////////////////////////////////////////////////////

// Returns the host's exit code, or -1 to carry on.
int Host::first(const Message &m)
{
  Reader r(m.payload);
  m_reply.clear();
  if (Op(m.header.opcode) == Op::QUERY_EXTENSIONS) {
    queryExtensions(r);
    platform::sendAll(m_call, m_reply.data(), m_reply.size());
    return 0;
  } else if (Op(m.header.opcode) != Op::CREATE_DEVICE)
    protocolError("unexpected first message");

  bool created = createDevice(r);
  if (!r.ok())
    protocolError("truncated first message");
  if (created) {
    // Marks where, among the status events, the device came to exist.
    Buffer marker;
    {
      Writer w(marker, Op::DEVICE_CREATED_EVENT);
    }
    sendEvent(marker);
  }
  if (!platform::sendAll(m_call, m_reply.data(), m_reply.size()) || !created)
    return 1;
  return -1;
}

bool Host::createDevice(Reader &r)
{
  const std::string library = r.string();
  const std::string subtype = r.string();

  std::string failure;
  m_library = anariLoadLibrary(library.c_str(), statusCallback, this);
  if (!m_library)
    failure = "cannot load ANARI library '" + library + "'";
  else {
    m_device = anariNewDevice(m_library, subtype.c_str());
    if (!m_device) {
      failure = "library '" + library + "' has no device '" + subtype + "'";
    } else if (auto *e = anariGetDeviceExtensions(m_library, subtype.c_str())) {
      for (; *e; ++e) {
        if (std::string(*e) == "ANARI_KHR_FRAME_COMPLETION_CALLBACK")
          m_hasCompletionCallback = true;
      }
    }
  }

  Writer w(m_reply, Op::CREATE_DEVICE);
  w.pod(uint8_t(failure.empty()));
  w.string(failure);
  return failure.empty();
}

void Host::queryExtensions(Reader &r)
{
  const std::string library = r.string();
  const std::string subtype = r.string();

  Writer w(m_reply, Op::QUERY_EXTENSIONS);
  m_library = anariLoadLibrary(library.c_str(), statusCallback, this);
  if (!m_library) {
    w.pod(uint8_t(0));
    w.string("cannot load ANARI library '" + library + "'");
    writeStringList(w, nullptr);
    return;
  }
  w.pod(uint8_t(1));
  w.string("");
  writeStringList(w, anariGetDeviceExtensions(m_library, subtype.c_str()));
}

// Object table ///////////////////////////////////////////////////////////////

void Host::add(ObjectId id, ANARIObject handle)
{
  if (id <= DEVICE_ID || m_objects.count(id))
    protocolError("object id reused");
  m_objects[id].handle = handle;
  if (handle) {
    std::lock_guard<std::mutex> lock(m_reverseMutex);
    m_reverse[handle] = id;
  }
}

ANARIObject Host::resolve(ObjectId id)
{
  if (id == NULL_ID)
    return nullptr;
  if (id == DEVICE_ID)
    return m_device;
  auto it = m_objects.find(id);
  if (it == m_objects.end())
    protocolError("unknown object id");
  return it->second.handle;
}

void Host::installCompletionCallback(ObjectId id, ANARIObject frame)
{
  if (!frame || !m_hasCompletionCallback)
    return;
  ANARIFrameCompletionCallback cb = frameCompleted;
  anariSetParameter(m_device,
      frame,
      "frameCompletionCallback",
      ANARI_FRAME_COMPLETION_CALLBACK,
      &cb);
  anariSetParameter(m_device,
      frame,
      "frameCompletionCallbackUserData",
      ANARI_VOID_POINTER,
      m_frames[id].get());
}

// Array contents arrive as raw bytes, except object arrays, whose elements are
// object ids to translate. With 'keepUnchanged', an element sent as
// UNCHANGED_ID is left as it is in 'dst'.
void Host::fillElements(void *dst,
    const uint8_t *src,
    ANARIDataType type,
    size_t bytes,
    bool keepUnchanged)
{
  if (!anari::isObject(type)) {
    std::memcpy(dst, src, bytes);
    return;
  }
  auto *out = static_cast<ANARIObject *>(dst);
  for (size_t i = 0; i < bytes / sizeof(ObjectId); i++) {
    ObjectId id;
    std::memcpy(&id, src + i * sizeof(ObjectId), sizeof(id));
    if (keepUnchanged && id == UNCHANGED_ID)
      continue;
    out[i] = resolve(id);
  }
}

// Calls //////////////////////////////////////////////////////////////////////

void Host::dispatch(Op op, Reader &r)
{
  auto create = [&](auto &&make) {
    ObjectId id = r.pod<ObjectId>();
    add(id, make());
    return id;
  };
  auto createTyped = [&](auto fn) {
    ObjectId id = r.pod<ObjectId>();
    std::string subtype = r.string();
    add(id, fn(m_device, subtype.c_str()));
  };

  switch (op) {
  case Op::NEW_ARRAY1D:
    newArray(1, r);
    break;
  case Op::NEW_ARRAY2D:
    newArray(2, r);
    break;
  case Op::NEW_ARRAY3D:
    newArray(3, r);
    break;
  case Op::NEW_GEOMETRY:
    createTyped(anariNewGeometry);
    break;
  case Op::NEW_MATERIAL:
    createTyped(anariNewMaterial);
    break;
  case Op::NEW_SAMPLER:
    createTyped(anariNewSampler);
    break;
  case Op::NEW_SPATIAL_FIELD:
    createTyped(anariNewSpatialField);
    break;
  case Op::NEW_VOLUME:
    createTyped(anariNewVolume);
    break;
  case Op::NEW_LIGHT:
    createTyped(anariNewLight);
    break;
  case Op::NEW_INSTANCE:
    createTyped(anariNewInstance);
    break;
  case Op::NEW_CAMERA:
    createTyped(anariNewCamera);
    break;
  case Op::NEW_RENDERER:
    createTyped(anariNewRenderer);
    break;
  case Op::NEW_SURFACE:
    create([&] { return anariNewSurface(m_device); });
    break;
  case Op::NEW_GROUP:
    create([&] { return anariNewGroup(m_device); });
    break;
  case Op::NEW_WORLD:
    create([&] { return anariNewWorld(m_device); });
    break;
  case Op::NEW_FRAME: {
    ObjectId id = create([&] { return anariNewFrame(m_device); });
    m_frames[id].reset(new FrameCookie{this, id, ANARIFrame(resolve(id))});
    installCompletionCallback(id, resolve(id));
    break;
  }
  case Op::NEW_OBJECT: {
    ObjectId id = r.pod<ObjectId>();
    std::string objectType = r.string();
    std::string subtype = r.string();
    add(id, anariNewObject(m_device, objectType.c_str(), subtype.c_str()));
    break;
  }
  case Op::SET_PARAMETER:
    setParameter(r);
    break;
  case Op::UNSET_PARAMETER: {
    ANARIObject o = resolve(r.pod<ObjectId>());
    std::string name = r.string();
    if (o)
      anariUnsetParameter(m_device, o, name.c_str());
    break;
  }
  case Op::UNSET_ALL_PARAMETERS: {
    ObjectId id = r.pod<ObjectId>();
    if (ANARIObject o = resolve(id)) {
      anariUnsetAllParameters(m_device, o);
      if (m_frames.count(id)) // that removed our own callback too
        installCompletionCallback(id, o);
    }
    break;
  }
  case Op::COMMIT_PARAMETERS:
    if (ANARIObject o = resolve(r.pod<ObjectId>()))
      anariCommitParameters(m_device, o);
    break;
  case Op::RELEASE:
    release(r);
    break;
  case Op::RETAIN: {
    ObjectId id = r.pod<ObjectId>();
    ANARIObject o = resolve(id);
    if (id == DEVICE_ID)
      protocolError("retain of the device");
    m_objects[id].count++;
    if (o)
      anariRetain(m_device, o);
    break;
  }
  case Op::UNMAP_ARRAY:
    unmapArray(r);
    break;
  case Op::GET_PROPERTY:
    getProperty(r);
    break;
  case Op::GET_OBJECT_SUBTYPES: {
    auto type = ANARIDataType(r.pod<int32_t>());
    Writer w(m_reply, op);
    writeValue(w, ANARI_STRING_LIST, anariGetObjectSubtypes(m_device, type));
    m_hasReply = true;
    break;
  }
  case Op::GET_OBJECT_INFO: {
    auto objectType = ANARIDataType(r.pod<int32_t>());
    OptionalString subtype(r);
    OptionalString infoName(r);
    auto infoType = ANARIDataType(r.pod<int32_t>());
    Writer w(m_reply, op);
    writeValue(w,
        infoType,
        anariGetObjectInfo(
            m_device, objectType, subtype.c_str(), infoName.c_str(), infoType));
    m_hasReply = true;
    break;
  }
  case Op::GET_PARAMETER_INFO: {
    auto objectType = ANARIDataType(r.pod<int32_t>());
    OptionalString subtype(r);
    OptionalString parameterName(r);
    auto parameterType = ANARIDataType(r.pod<int32_t>());
    OptionalString infoName(r);
    auto infoType = ANARIDataType(r.pod<int32_t>());
    Writer w(m_reply, op);
    writeValue(w,
        infoType,
        anariGetParameterInfo(m_device,
            objectType,
            subtype.c_str(),
            parameterName.c_str(),
            parameterType,
            infoName.c_str(),
            infoType));
    m_hasReply = true;
    break;
  }
  case Op::FRAME_BUFFER_MAP:
    frameBufferMap(r);
    break;
  case Op::RENDER_FRAME:
    renderFrame(r);
    break;
  case Op::DISCARD_FRAME:
    if (ANARIObject o = resolve(r.pod<ObjectId>()))
      anariDiscardFrame(m_device, ANARIFrame(o));
    break;
  case Op::RELEASE_DEVICE: {
    anariRelease(m_device, m_device);
    anariUnloadLibrary(m_library);
    m_device = nullptr; // probes still queued find nothing to ask
    Writer w(m_reply, op);
    m_hasReply = true;
    break;
  }
  default:
    protocolError("unknown opcode");
  }
}

int Host::takeDescriptor()
{
  int descriptor = m_descriptor;
  m_descriptor = -1;
  return descriptor;
}

void Host::newArray(int dims, Reader &r)
{
  ObjectId id = r.pod<ObjectId>();
  auto type = ANARIDataType(r.pod<int32_t>());
  uint64_t n[3] = {1, 1, 1};
  for (int i = 0; i < dims; i++)
    n[i] = r.pod<uint64_t>();
  const Storage storage = r.pod<Storage>();
  const size_t bytes = anari::sizeOf(type) * n[0] * n[1] * n[2];

  // The wrapped device only ever sees shared arrays: over the adapter's
  // region where it can read that as it is, over host memory otherwise.
  auto *backing = new ArrayBacking;
  const uint8_t *data = nullptr;
  if (storage == Storage::INLINE)
    data = r.bytes(bytes);
  else if (storage == Storage::REGION) {
    const uint64_t offset = r.pod<uint64_t>();
    const uint64_t length = r.pod<uint64_t>();
    const int descriptor = takeDescriptor();
    const bool mapped =
        platform::Region::map(descriptor, true, backing->region);
    platform::closeDescriptor(descriptor);
    if (descriptor < 0 || !r.ok())
      protocolError("array in a region, without the region");
    if (!mapped)
      fatal("cannot map an array's region");
    if (length != bytes || offset > backing->region.size()
        || length > backing->region.size() - offset)
      protocolError("array does not fit its region");
    data = backing->contents =
        static_cast<const uint8_t *>(backing->region.data()) + offset;
  }
  if (!r.ok()) {
    delete backing;
    return;
  }

  void *mem = const_cast<uint8_t *>(backing->contents);
  if (!mem || anari::isObject(type)) {
    mem = backing->heap = std::calloc(bytes ? bytes : 1, 1);
    if (!mem)
      fatal("out of memory for an array");
    if (data)
      fillElements(mem, data, type, bytes);
    // The ids were this message's only: see the adapter's newArray().
    backing->contents = nullptr;
    backing->region = platform::Region();
  }

  ANARIObject a = nullptr;
  if (dims == 1)
    a = anariNewArray1D(m_device, mem, freeArrayBacking, backing, type, n[0]);
  else if (dims == 2) {
    a = anariNewArray2D(
        m_device, mem, freeArrayBacking, backing, type, n[0], n[1]);
  } else {
    a = anariNewArray3D(
        m_device, mem, freeArrayBacking, backing, type, n[0], n[1], n[2]);
  }
  if (a)
    m_arrays[id] = backing;
  else
    delete backing;
  add(id, a);
}

void Host::unmapArray(Reader &r)
{
  const ObjectId id = r.pod<ObjectId>();
  ANARIObject a = resolve(id);
  auto type = ANARIDataType(r.pod<int32_t>());
  uint64_t bytes = r.pod<uint64_t>();
  const Storage storage = r.pod<Storage>();

  // New contents to copy in: sent inline, or in a region. An object array's
  // ids come in a region made for this message alone. Any other array is
  // either read in place by the wrapped device, which only has to be told
  // that it changed, or is held in host memory and has a region of its own
  // from the first time it is written again: that one arrives with this
  // message, stays, and is copied from now and at every later unmap.
  auto found = m_arrays.find(id);
  ArrayBacking *backing = found == m_arrays.end() ? nullptr : found->second;
  const uint8_t *data = nullptr;
  platform::Region ids;
  if (storage == Storage::INLINE)
    data = r.bytes(bytes);
  else if (storage == Storage::REGION) {
    const uint64_t offset = r.pod<uint64_t>();
    const uint64_t length = r.pod<uint64_t>();
    const int descriptor = takeDescriptor();
    if (descriptor >= 0) {
      const bool objects = anari::isObject(type);
      platform::Region arrived;
      const bool mapped = platform::Region::map(descriptor, false, arrived);
      platform::closeDescriptor(descriptor);
      if (!r.ok())
        protocolError("array in a region, without its place there");
      if (!mapped)
        fatal("cannot map an array's region");
      if (length != bytes || offset > arrived.size()
          || length > arrived.size() - offset)
        protocolError("array does not fit its region");
      data = static_cast<const uint8_t *>(arrived.data()) + offset;
      if (objects)
        ids = std::move(arrived);
      else if (backing) {
        if (backing->region || !backing->heap)
          protocolError("a region for an array that has one");
        backing->region = std::move(arrived);
        backing->staged = data;
      } else
        data = nullptr; // an array the wrapped device never made
    } else if (a) {
      if (!r.ok() || !backing || !(backing->contents || backing->staged))
        protocolError("array has no region");
      if (backing->staged) {
        if (length != bytes || offset != 0 || length > backing->region.size())
          protocolError("array does not fit its region");
        data = backing->staged;
      }
    }
  }
  if (!r.ok() || !a)
    return;

  void *mem = anariMapArray(m_device, ANARIArray(a));
  if (mem && data)
    fillElements(mem, data, type, bytes, true);
  anariUnmapArray(m_device, ANARIArray(a));
}

void Host::setParameter(Reader &r)
{
  ANARIObject o = resolve(r.pod<ObjectId>());
  std::string name = r.string();
  auto type = ANARIDataType(r.pod<int32_t>());

  if (anari::isObject(type)) {
    ObjectId valueId = r.pod<ObjectId>();
    ANARIObject value = resolve(valueId);
    if (o && (value || valueId == NULL_ID))
      anariSetParameter(m_device, o, name.c_str(), type, &value);
  } else if (type == ANARI_STRING) {
    std::string value = r.string();
    if (o)
      anariSetParameter(m_device, o, name.c_str(), type, value.c_str());
  } else if (type == ANARI_STRING_LIST) {
    uint32_t count = r.pod<uint32_t>();
    std::vector<std::string> strings;
    for (uint32_t i = 0; i < count && r.ok(); i++)
      strings.push_back(r.string());
    std::vector<const char *> pointers;
    for (auto &s : strings)
      pointers.push_back(s.c_str());
    pointers.push_back(nullptr);
    if (o)
      anariSetParameter(m_device, o, name.c_str(), type, pointers.data());
  } else {
    const size_t size = anari::sizeOf(type);
    const uint8_t *data = r.bytes(size);
    if (!data || size == 0)
      protocolError("parameter value of the wrong size");
    Buffer aligned(data, data + size); // the payload is not aligned for 'type'
    if (o)
      anariSetParameter(m_device, o, name.c_str(), type, aligned.data());
  }
}

void Host::getProperty(Reader &r)
{
  ANARIObject o = resolve(r.pod<ObjectId>());
  std::string name = r.string();
  auto type = ANARIDataType(r.pod<int32_t>());
  uint64_t size = r.pod<uint64_t>();
  auto mask = ANARIWaitMask(r.pod<int32_t>());
  if (!r.ok())
    return;

  Writer w(m_reply, Op::GET_PROPERTY);
  m_hasReply = true;

  if (type == ANARI_STRING_LIST) {
    const char *const *list = nullptr;
    int found = o
        ? anariGetProperty(
              m_device, o, name.c_str(), type, &list, sizeof(list), mask)
        : 0;
    w.pod(int32_t(found));
    writeValue(w, type, found ? list : nullptr);
  } else {
    Buffer value(size);
    int found = o
        ? anariGetProperty(
              m_device, o, name.c_str(), type, value.data(), size, mask)
        : 0;
    w.pod(int32_t(found));
    w.bytes(value.data(), value.size());
  }
}

void Host::frameBufferMap(Reader &r)
{
  const ObjectId frameId = r.pod<ObjectId>();
  ANARIObject f = resolve(frameId);
  std::string channel = r.string();
  if (!r.ok())
    return;

  uint32_t width = 0, height = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  const void *pixels = f
      ? anariMapFrame(
            m_device, ANARIFrame(f), channel.c_str(), &width, &height, &type)
      : nullptr;
  const uint64_t bytes =
      pixels ? uint64_t(width) * height * anari::sizeOf(type) : 0;

  // Large channels are copied into a region kept for this frame and channel,
  // made anew (and its descriptor sent) when the size is not the last one.
  // If the system will not provide a region the pixels go inline.
  platform::Region *region = nullptr;
  bool isNew = false;
  const auto slot = std::make_pair(frameId, channel);
  if (bytes >= FRAME_REGION_THRESHOLD) {
    auto it = m_frameRegions.find(slot);
    if (it != m_frameRegions.end() && it->second.size() == bytes)
      region = &it->second;
    else {
      platform::Region fresh;
      if (platform::Region::create(bytes, fresh)) {
        region = &(m_frameRegions[slot] = std::move(fresh));
        isNew = true;
      }
    }
  }
  if (!region)
    m_frameRegions.erase(slot);

  {
    Writer w(m_reply, Op::FRAME_BUFFER_MAP, isNew ? 1 : 0);
    m_hasReply = true;
    w.pod(width);
    w.pod(height);
    w.pod(int32_t(type));
    if (region) {
      std::memcpy(region->data(), pixels, bytes);
      w.pod(Storage::REGION);
      w.pod(uint64_t(0));
      w.pod(bytes);
    } else if (pixels) {
      w.pod(Storage::INLINE);
      w.pod(bytes);
      w.bytes(pixels, bytes);
    } else
      w.pod(Storage::NONE);
  }
  if (isNew) {
    // Sent with the reply; the mapping is all the host keeps.
    m_replyDescriptor = region->releaseDescriptor();
  }

  // The adapter reads the copy, so the wrapped mapping ends here.
  if (f)
    anariUnmapFrame(m_device, ANARIFrame(f), channel.c_str());
}

void Host::renderFrame(Reader &r)
{
  ObjectId id = r.pod<ObjectId>();
  resolve(id);
  auto it = m_frames.find(id);
  if (it == m_frames.end())
    protocolError("render of an object that is not a frame");
  FrameCookie &cookie = *it->second;

  if (cookie.rendered == cookie.completed)
    m_rendering.push_back(&cookie);
  cookie.rendered++;
  if (!cookie.frame) {
    // Nothing to render, and the adapter still waits for a completion.
    complete(cookie);
    return;
  }
  anariRenderFrame(m_device, cookie.frame);

  // A wrapped device that declined at once is found here; one still
  // rendering, or declining later, by the probes that follow.
  probeFrame(cookie);
}

void Host::release(Reader &r)
{
  ObjectId id = r.pod<ObjectId>();
  ANARIObject o = resolve(id);
  if (id == DEVICE_ID)
    protocolError("release of the device as an object");
  if (id == NULL_ID)
    return;
  if (o)
    anariRelease(m_device, o);
  auto it = m_objects.find(id);
  if (--it->second.count == 0) {
    if (o) {
      std::lock_guard<std::mutex> lock(m_reverseMutex);
      m_reverse.erase(o);
    }
    m_objects.erase(it);
    // The frame's cookie stays: the wrapped device may still be rendering it.
    // Nobody waits for that any more, so the host stops asking.
    auto frame = m_frames.find(id);
    if (frame != m_frames.end()) {
      frame->second->frame = nullptr;
      complete(*frame->second);
    }
    // Its regions go: nothing can ask for the frame to be mapped again.
    auto first = m_frameRegions.lower_bound({id, std::string()});
    auto last = first;
    while (last != m_frameRegions.end() && last->first.first == id)
      ++last;
    m_frameRegions.erase(first, last);
    // An array's backing is the wrapped device's to free from here on.
    m_arrays.erase(id);
  }
}

// Replies and events /////////////////////////////////////////////////////////

void Host::writeStringList(Writer &w, const char *const *list)
{
  uint32_t count = 0;
  while (list && list[count])
    count++;
  w.pod(count);
  for (uint32_t i = 0; i < count; i++)
    w.string(list[i]);
}

void Host::writeValue(Writer &w, ANARIDataType type, const void *p)
{
  if (!p) {
    w.pod(ValueKind::NONE);
    return;
  }
  switch (type) {
  case ANARI_STRING:
    w.pod(ValueKind::STRING);
    w.string(static_cast<const char *>(p));
    break;
  case ANARI_STRING_LIST:
    w.pod(ValueKind::STRING_LIST);
    writeStringList(w, static_cast<const char *const *>(p));
    break;
  case ANARI_DATA_TYPE_LIST: {
    auto *list = static_cast<const ANARIDataType *>(p);
    uint32_t count = 0;
    while (list[count] != ANARI_UNKNOWN)
      count++;
    w.pod(ValueKind::DATA_TYPE_LIST);
    w.pod(count);
    for (uint32_t i = 0; i < count; i++)
      w.pod(int32_t(list[i]));
    break;
  }
  case ANARI_PARAMETER_LIST: {
    auto *list = static_cast<const ANARIParameter *>(p);
    uint32_t count = 0;
    while (list[count].name)
      count++;
    w.pod(ValueKind::PARAMETER_LIST);
    w.pod(count);
    for (uint32_t i = 0; i < count; i++) {
      w.string(list[i].name);
      w.pod(int32_t(list[i].type));
    }
    break;
  }
  default: {
    const size_t size = anari::sizeOf(type);
    if (size == 0) {
      w.pod(ValueKind::NONE);
      break;
    }
    w.pod(ValueKind::BYTES);
    w.pod(uint32_t(size));
    w.bytes(p, size);
  }
  }
}

void Host::sendEvent(const Buffer &b)
{
  // Called from whichever thread the wrapped device reports on. A failed send
  // means the adapter is gone, which the main loop finds out for itself.
  std::lock_guard<std::mutex> lock(m_eventMutex);
  platform::sendAll(m_event, b.data(), b.size());
}

void Host::sendFrameEvent(Op kind, ObjectId id, const uint64_t *render)
{
  Buffer b;
  {
    Writer w(b, kind);
    w.pod(id);
    if (render)
      w.pod(*render);
  }
  sendEvent(b);
}

void Host::statusCallback(const void *userData,
    ANARIDevice,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  auto *host = static_cast<Host *>(const_cast<void *>(userData));

  // An object the adapter has no id for is reported as the device.
  ObjectId id = DEVICE_ID;
  {
    std::lock_guard<std::mutex> lock(host->m_reverseMutex);
    auto it = host->m_reverse.find(source);
    if (it != host->m_reverse.end())
      id = it->second;
  }
  if (id == DEVICE_ID)
    sourceType = ANARI_DEVICE;

  Buffer b;
  {
    Writer w(b, Op::STATUS_EVENT);
    w.pod(id);
    w.pod(int32_t(sourceType));
    w.pod(int32_t(severity));
    w.pod(int32_t(code));
    w.string(message ? message : "");
  }
  host->sendEvent(b);
}

void Host::frameCompleted(const void *userData, ANARIDevice, ANARIFrame)
{
  // Says only that the wrapped device called back, which is when the
  // application's callback runs. Whether the render is over is for the worker
  // to ask; told first, the adapter runs the callback before it hears that.
  auto &cookie = *static_cast<FrameCookie *>(const_cast<void *>(userData));
  cookie.host->sendFrameEvent(Op::FRAME_CALLED_BACK_EVENT, cookie.id);
  cookie.host->postProbe();
}

} // namespace

int runHost(int callEndpoint, int eventEndpoint, CallObserver *observer)
{
  Host host(callEndpoint, eventEndpoint, observer);
  return host.run();
}

} // namespace anari_guard
