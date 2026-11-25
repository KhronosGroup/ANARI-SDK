// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "adapter/HostConnection.h"

// anari
#include <anari/backend/DeviceImpl.h>
// std
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace anari_guard {

// A value the API hands back by pointer, kept alive until the device goes.
struct Interned
{
  Buffer bytes;
  std::vector<std::string> strings;
  std::vector<const char *> pointers;
  std::vector<ANARIDataType> types;
  std::vector<ANARIParameter> parameters;
  const void *value{nullptr};
};

// Decode a type-tagged value from a reply into 'out'.
void decodeValue(Reader &r, Interned &out);

struct GuardDevice : public anari::DeviceImpl
{
  // Start a host, have it create the wrapped device, and return the guard
  // device standing in for it; nullptr with 'error' set on any failure.
  static GuardDevice *create(
      ANARILibrary library, const WrappedDevice &wrapped, std::string &error);

  // Wait for every device that was released from inside one of the
  // application's callbacks to finish taking itself down. Called when the
  // library is unloaded, after which none of this code may still be running.
  static void waitForReleasesInCallbacks();

  // Object creation //////////////////////////////////////////////////////////

  ANARIArray1D newArray1D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1) override;
  ANARIArray2D newArray2D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2) override;
  ANARIArray3D newArray3D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3) override;
  ANARIGeometry newGeometry(const char *type) override;
  ANARIMaterial newMaterial(const char *type) override;
  ANARISampler newSampler(const char *type) override;
  ANARISurface newSurface() override;
  ANARISpatialField newSpatialField(const char *type) override;
  ANARIVolume newVolume(const char *type) override;
  ANARILight newLight(const char *type) override;
  ANARIGroup newGroup() override;
  ANARIInstance newInstance(const char *type) override;
  ANARIWorld newWorld() override;
  ANARICamera newCamera(const char *type) override;
  ANARIRenderer newRenderer(const char *type) override;
  ANARIFrame newFrame() override;
  ANARIObject newObject(const char *objectType, const char *type) override;

  // Parameters and lifetime //////////////////////////////////////////////////

  void setParameter(ANARIObject object,
      const char *name,
      ANARIDataType type,
      const void *mem) override;
  void unsetParameter(ANARIObject object, const char *name) override;
  void unsetAllParameters(ANARIObject object) override;
  void *mapParameterArray1D(ANARIObject object,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t *elementStride) override;
  void *mapParameterArray2D(ANARIObject object,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t *elementStride) override;
  void *mapParameterArray3D(ANARIObject object,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t numElements3,
      uint64_t *elementStride) override;
  void unmapParameterArray(ANARIObject object, const char *name) override;
  void commitParameters(ANARIObject object) override;
  void release(ANARIObject object) override;
  void retain(ANARIObject object) override;
  void *mapArray(ANARIArray) override;
  void unmapArray(ANARIArray) override;
  int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask) override;

  // Introspection ////////////////////////////////////////////////////////////

  const char **getObjectSubtypes(ANARIDataType objectType) override;
  const void *getObjectInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *infoName,
      ANARIDataType infoType) override;
  const void *getParameterInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *parameterName,
      ANARIDataType parameterType,
      const char *infoName,
      ANARIDataType infoType) override;

  // Frames ///////////////////////////////////////////////////////////////////

  const void *frameBufferMap(ANARIFrame fb,
      const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;
  void frameBufferUnmap(ANARIFrame fb, const char *channel) override;
  void renderFrame(ANARIFrame) override;
  int frameReady(ANARIFrame, ANARIWaitMask) override;
  void discardFrame(ANARIFrame) override;

 private:
  // What the application maps is its own memory (a shared array), else
  // 'owned' when that is in use, else the region. What the host reads is the
  // region when there is one, else bytes sent inline.
  //
  // An array at or above the array ceiling has its region from the start.
  // One below it crosses inline the first time and is given a region when it
  // is written again: a shared array at its first unmap, a managed array at
  // its second mapping, which moves it out of 'owned'. Object arrays never
  // keep a region.
  struct ArrayState
  {
    ANARIDataType type{ANARI_UNKNOWN};
    size_t bytes{0};
    const void *appMemory{nullptr}; // shared arrays
    ANARIMemoryDeleter deleter{nullptr};
    const void *deleterUserData{nullptr};
    // Managed arrays below the ceiling that have been written at most once,
    // and those that hold object handles (which the host must be sent as ids,
    // so never straight from where the application writes them)
    Buffer owned;
    // A managed array's contents have crossed inline, from 'owned', once
    bool filled{false};
    // The contents as the host reads them, except object handles: their ids
    // cross inline, or in a region made for each message. While the host has
    // yet to be sent the region, its descriptor is open.
    platform::Region region;
    // Object arrays: the id last sent to the host for each element. An
    // element that still equals it at an unmap is sent as UNCHANGED_ID.
    std::vector<ObjectId> sent;

    void *mapped()
    {
      return appMemory     ? const_cast<void *>(appMemory)
          : !owned.empty() ? owned.data()
                           : region.data();
    }
    // Where the region's contents are copied from, if anywhere
    const void *source() const
    {
      return appMemory ? appMemory : owned.empty() ? nullptr : owned.data();
    }
  };

  struct FrameState
  {
    std::mutex mutex;
    std::condition_variable changed;
    // How many renders the application has asked for. The host counts the
    // same, and a completion says which one it is for: only the latest ends
    // anything.
    uint64_t renders{0};
    bool hostRendering{false}; // cleared by the reader
    bool inFlight{false}; // cleared by the dispatcher, in event order
    // The wrapped device has called back since the latest render was asked for
    bool calledBack{false};
    // The application has released the frame for the last time. A thread
    // still waiting on it then is misuse, and is let go rather than left.
    bool released{false};
    ANARIFrameCompletionCallback pendingCallback{nullptr};
    const void *pendingUserData{nullptr};
    ANARIFrameCompletionCallback callback{nullptr};
    const void *userData{nullptr};
    // Mapped channels, by name: pixels that came inline, and the regions the
    // host copies larger channels into. A region stays for the next mapping
    // of its channel; both stay readable after device death.
    std::map<std::string, Buffer> mapped;
    std::map<std::string, platform::Region> regions;
  };

  struct ParameterMapping
  {
    int dims{1};
    ANARIDataType type{ANARI_UNKNOWN};
    uint64_t n[3]{1, 1, 1};
    size_t bytes{0};
    Buffer data; // what the application writes to, unless it is the region
    platform::Region region;
  };

  struct Event
  {
    Op kind{Op::STATUS_EVENT};
    bool stop{false};
    // Queued at device death: everything ahead of it was queued before
    bool death{false};
    // Said while the wrapped device was being created: the application has
    // no guard device yet, and may never get one.
    bool beforeDevice{false};
    ObjectId id{NULL_ID};
    ANARIDataType sourceType{ANARI_DEVICE};
    ANARIStatusSeverity severity{ANARI_SEVERITY_INFO};
    ANARIStatusCode code{ANARI_STATUS_NO_ERROR};
    std::string message;
    std::shared_ptr<FrameState> frame;
    uint64_t render{0}; // which of the frame's renders a completion is for
  };

  GuardDevice(ANARILibrary library,
      const HostConnection &host,
      const WrappedDevice &wrapped);
  ~GuardDevice() override = default;

  // Handles //////////////////////////////////////////////////////////////////

  ObjectId idOf(ANARIObject object) const;
  ANARIObject handleOf(ObjectId id) const;
  // An id for an object the application is given a handle to, held once.
  ObjectId newId();
  // An id the application never sees.
  ObjectId newInternalId();
  std::shared_ptr<FrameState> frameState(ObjectId id);

  // The application's handles are checked before anything is sent, so that
  // one it was never given, or has released for the last time, is its own
  // error and not the host's. Each of these reports the misuse, naming the
  // API call, and returns false (or nothing) for the caller to do nothing.
  void refuse(const char *call, const char *what);
  bool target(ANARIObject object, const char *call, ObjectId &id);
  std::shared_ptr<FrameState> frameFor(ANARIFrame frame, const char *call);
  // The ids of 'count' handles, written to 'ids' (which need not be aligned);
  // NULL_ID stands in for a handle that is not this device's.
  // With 'sent', an object array's record of what the host was last sent,
  // the record is brought up to date. With 'unchanged' as well, a handle that
  // still equals its record is not checked and goes as UNCHANGED_ID: it means
  // what it meant, whether or not the application still holds it.
  void idsOf(void *ids,
      const ANARIObject *handles,
      size_t count,
      const char *call,
      ObjectId *sent = nullptr,
      bool unchanged = false);

  // Call channel /////////////////////////////////////////////////////////////

  // Buffer one pipelined call; 'fill' writes its fields. Nothing happens on
  // a dead device.
  template <typename F>
  void send(Op op, F &&fill, bool flushNow = false);
  // Send one call and wait for its reply. False, with no reply, if the device
  // is dead or dies before the reply arrives.
  // A descriptor that comes with the reply is stored in 'descriptor', which
  // the caller then owns.
  template <typename F>
  bool call(Op op, F &&fill, Buffer &reply, int *descriptor = nullptr);
  // Send one call that carries a descriptor: after everything buffered, and
  // on its own. Nothing happens on a dead device.
  template <typename F>
  void sendWithDescriptor(Op op, F &&fill, int descriptor); // m_callMutex held
  // Buffer one pipelined call that ends in bulk data sent inline; 'fill'
  // writes the fields ahead of it. A large payload does not go through the
  // buffer: everything buffered is sent, and then the payload from where it
  // is. Nothing happens on a dead device.
  template <typename F>
  void sendWithPayload(
      Op op, F &&fill, const void *payload, size_t bytes); // m_callMutex held
  // An introspection query: answered from the cache when it was asked before.
  template <typename F>
  const void *introspect(Op op, F &&fill, bool extensionList = false);
  void flush(); // m_callMutex held
  void sendIfFull(); // m_callMutex held

  // Traffic counters /////////////////////////////////////////////////////////

  // Always on, and read by the application as properties of the device named
  // "guard.bytesSent", "guard.bytesReceived", "guard.flushes" and
  // "guard.regionsCreated", each an ANARI_UINT64. The names are not a
  // promise.

  // One write of 'bytes' bytes to the call channel is about to be made.
  void countSent(size_t bytes);
  // recvMessage() on either channel, counted.
  bool receive(int endpoint,
      MessageHeader &header,
      Buffer &payload,
      int *descriptor = nullptr);
  // Region::create(), counted.
  bool newRegion(size_t bytes, platform::Region &out);
  // The counter the property 'name' reads; false if it names none.
  bool counter(const char *name, uint64_t &value) const;

  template <typename T>
  T create(Op op, const char *subtype, bool hasSubtype = true);
  ObjectId newArray(int dims,
      const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType type,
      const uint64_t *n);
  void *mapParameterArray(ANARIObject object,
      const char *name,
      int dims,
      ANARIDataType type,
      const uint64_t *n,
      uint64_t *elementStride);
  // 'sent' and 'unchanged': as for idsOf().
  void writeElements(Writer &w,
      const void *data,
      ANARIDataType type,
      size_t bytes,
      const char *call,
      ObjectId *sent = nullptr,
      bool unchanged = false);
  void copyElements(void *to,
      const void *from,
      ANARIDataType type,
      size_t bytes,
      const char *call,
      ObjectId *sent = nullptr,
      bool unchanged = false);
  // Keep a value from a reply for as long as the device lasts. An extension
  // list is kept without what the guard device does not forward.
  const void *intern(Reader &r, bool extensionList = false);

  // Events ///////////////////////////////////////////////////////////////////

  void startThreads();
  void readEvents();
  void dispatchEvents();
  void post(Event &&e);
  void report(ObjectId source,
      ANARIStatusSeverity severity,
      ANARIStatusCode code,
      const std::string &message);

  // Lifetime /////////////////////////////////////////////////////////////////

  // The transition to a dead device, made once by whichever thread first
  // sees the host gone or a channel fail. Safe to call from any thread, with
  // or without m_callMutex held.
  void markDead(bool hostSeenEnded = false);
  // The second half of device death, run once the event channel is drained:
  // words the death message, for the application's next call to deliver, and
  // completes the frames in flight.
  void announceDeath();
  // Every call the application makes is counted in and out, and on its way
  // out of a dead device delivers the death message if no call has yet.
  struct Entry
  {
    explicit Entry(GuardDevice *d) : device(d)
    {
      d->m_callers++;
    }
    // What the application's status callback throws goes on to the caller.
    ~Entry() noexcept(false)
    {
      device->leave();
    }
    Entry(const Entry &) = delete;
    Entry &operator=(const Entry &) = delete;
    GuardDevice *device;
  };
  void leave(); // may be the end of the device
  // Tell the application of device death, on this thread, unless another call
  // has or there is no status callback to tell. Waits first for the dispatcher
  // to deliver what was queued before death. Returns what the callback threw.
  std::exception_ptr deliverDeath(bool &delivered) noexcept;
  void wakeDeathWaiters();
  void releaseReference();
  // The application's last release of the device, in two parts: let go of
  // the host and the reader, then of the dispatcher. The second differs when
  // the release was made on the dispatcher, from inside a callback.
  void releaseHost();
  void releaseDevice(); // deletes the device
  void finishReleaseOnDispatcher();
  void abandon();

  HostConnection m_host;
  const WrappedDevice m_wrapped;

  std::atomic<bool> m_dead{false};
  std::atomic<bool> m_killedByAdapter{false};
  std::atomic<bool> m_announceStarted{false};

  // How the application is told. All but the atomics under m_deathMutex.
  std::mutex m_deathMutex;
  std::condition_variable m_deathChanged;
  std::string m_deathMessage;
  bool m_announced{false}; // the message is worded
  bool m_drained{false}; // the dispatcher is past everything from before
  bool m_delivered{false}; // the application's callback has returned
  std::atomic<bool> m_deathClaimed{false}; // by the call that delivers
  std::atomic<std::thread::id> m_deliveringThread{};
  // The device's last release came from inside the death message's callback;
  // the delivering thread's alone
  bool m_releasedInDelivery{false};
  // Calls the application is inside of, on any thread
  std::atomic<int> m_callers{0};

  // Creation resolves one way or the other before the reader may conclude
  // anything from the end of the event channel.
  std::mutex m_creationMutex;
  std::condition_variable m_creationResolved;
  bool m_created{false};
  bool m_abandoned{false};
  std::atomic<uint32_t> m_references{1};
  std::atomic<ObjectId> m_nextId{DEVICE_ID + 1};
  std::atomic<bool> m_releasing{false};
  // The last release came from inside a callback; the dispatcher's alone
  bool m_releasedInCallback{false};

  // Bytes written to the call channel, and how many writes that took
  std::atomic<uint64_t> m_bytesSent{0};
  std::atomic<uint64_t> m_flushes{0};
  // Bytes read from both channels
  std::atomic<uint64_t> m_bytesReceived{0};
  // Regions made for this device, by the adapter or by the host
  std::atomic<uint64_t> m_regionsCreated{0};

  // Covers the buffer of pipelined calls and the call channel
  std::mutex m_callMutex;
  Buffer m_out;

  // Covers the side tables below
  std::mutex m_tableMutex;
  // Every object the application holds a handle to, with how many times
  std::unordered_map<ObjectId, uint32_t> m_live;
  std::unordered_map<ObjectId, ArrayState> m_arrays;
  std::unordered_map<ObjectId, std::shared_ptr<FrameState>> m_frames;
  std::map<std::pair<ObjectId, std::string>, ParameterMapping> m_mappings;
  std::map<std::string, std::unique_ptr<Interned>> m_interned;
  std::map<std::string, const void *> m_introspection; // by request bytes

  std::mutex m_statusMutex;
  ANARIStatusCallback m_pendingStatusCallback{nullptr};
  const void *m_pendingStatusUserData{nullptr};
  bool m_statusCommitted{false};
  ANARIStatusCallback m_statusCallback{nullptr};
  const void *m_statusUserData{nullptr};

  std::mutex m_queueMutex;
  std::condition_variable m_queueChanged;
  std::deque<Event> m_queue;

  std::thread m_reader;
  std::thread m_dispatcher;
  std::thread::id m_dispatcherId; // written under m_queueMutex, once
};

} // namespace anari_guard
