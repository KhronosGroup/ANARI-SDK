// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The recording device: an ANARI library ('guard_recorder') for the guard
// device to wrap in tests. It renders nothing. It writes every call it
// receives, arguments and all, as one line to the file named by
// GUARD_RECORDER_LOG, and answers queries with fixed values, so a test can
// compare what an application called with what the wrapped device was given.
//
// It lists ANARI_KHR_DEVICE_SYNCHRONIZATION and has no lock: only ever wrapped
// by the guard device, it is only ever called by the host's one worker.
//
// Objects are named in the log by the order they were created in: #1, #2, ...
// Not logged, because how often the host makes them is the host's business:
// frameReady, mapArray, and the frame completion callback parameters.
//
// The device subtype 'slow' is a device without the frame completion callback
// extension whose every render takes 200 ms. The subtype 'late' has the
// extension and calls back at once, and its renders too are over 200 ms later.

#include <anari/backend/DeviceImpl.h>
#include <anari/backend/LibraryImpl.h>
#include <anari/frontend/type_utility.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

const char *EXTENSIONS[] = {"ANARI_KHR_GEOMETRY_TRIANGLE",
    "ANARI_NV_ARRAY_CUDA",
    "ANARI_KHR_FRAME_COMPLETION_CALLBACK",
    "ANARI_NV_FRAME_BUFFERS_CUDA",
    "ANARI_KHR_DATA_PARALLEL_MPI",
    "ANARI_KHR_CAMERA_PERSPECTIVE",
    "ANARI_KHR_DEVICE_SYNCHRONIZATION",
    nullptr};

const char *SLOW_EXTENSIONS[] = {"ANARI_KHR_CAMERA_PERSPECTIVE", nullptr};

constexpr auto SLOW_RENDER = std::chrono::milliseconds(200);

std::string quoted(const char *s)
{
  return s ? '"' + std::string(s) + '"' : std::string("null");
}

uint64_t fnv1a(const void *data, size_t size)
{
  auto *p = static_cast<const uint8_t *>(data);
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < size; i++)
    h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

// Up to 32 bytes in hex, a checksum beyond that.
std::string bytesText(const void *data, size_t size)
{
  char text[32];
  if (size > 32) {
    std::snprintf(text,
        sizeof(text),
        "fnv=%016llx",
        (unsigned long long)fnv1a(data, size));
    return text;
  }
  std::string out = "[";
  auto *p = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < size; i++) {
    std::snprintf(text, sizeof(text), "%02x", p[i]);
    out += text;
  }
  return out + "]";
}

struct Recorder : public anari::DeviceImpl
{
  struct Object
  {
    ANARIDataType type{ANARI_OBJECT};
    uint32_t references{1};
    // Arrays
    const void *memory{nullptr};
    ANARIMemoryDeleter deleter{nullptr};
    const void *deleterUserData{nullptr};
    ANARIDataType elementType{ANARI_UNKNOWN};
    size_t bytes{0};
    // Frames
    uint32_t size[2]{0, 0};
    uint32_t renders{0};
    ANARIFrameCompletionCallback callback{nullptr};
    const void *callbackUserData{nullptr};
    std::vector<uint8_t> color;
    std::vector<float> depth;
  };

  Recorder(ANARILibrary library, bool slow, bool late)
      : anari::DeviceImpl(library), m_slow(slow), m_late(late)
  {
    if (const char *path = std::getenv("GUARD_RECORDER_LOG"))
      m_log = std::fopen(path, "a");
    status(this_device(), ANARI_DEVICE, "recorder: device constructed");
  }

  ~Recorder() override
  {
    if (m_log)
      std::fclose(m_log);
  }

  // Helpers //////////////////////////////////////////////////////////////////

  void log(const std::string &line)
  {
    if (!m_log)
      return;
    std::fputs((line + "\n").c_str(), m_log);
    std::fflush(m_log);
  }

  void status(ANARIObject source, ANARIDataType type, const char *message)
  {
    if (auto cb = defaultStatusCallback()) {
      cb(defaultStatusCallbackUserPtr(),
          this_device(),
          source,
          type,
          ANARI_SEVERITY_WARNING,
          ANARI_STATUS_INVALID_OPERATION,
          message);
    }
  }

  std::string name(ANARIObject o) const
  {
    if (!o)
      return "null";
    if (handleIsDevice(o))
      return "device";
    return "#" + std::to_string(uintptr_t(o));
  }

  Object *find(ANARIObject o)
  {
    auto it = m_objects.find(uintptr_t(o));
    return it == m_objects.end() ? nullptr : &it->second;
  }

  template <typename T>
  T make(ANARIDataType type, const std::string &call)
  {
    const uintptr_t handle = ++m_last;
    m_objects[handle].type = type;
    log(call + " -> #" + std::to_string(handle));
    return T(handle);
  }

  template <typename T>
  T makeTyped(ANARIDataType type, const char *call, const char *subtype)
  {
    // The one creation that fails, as a wrapped device's may.
    if (subtype && std::strcmp(subtype, "refused") == 0) {
      log(std::string(call) + " " + quoted(subtype) + " -> null");
      return T(nullptr);
    }
    return make<T>(type, std::string(call) + " " + quoted(subtype));
  }

  std::string elementsText(
      const void *data, ANARIDataType type, size_t bytes) const
  {
    if (!data)
      return "null";
    if (!anari::isObject(type))
      return bytesText(data, bytes);
    std::string out = "[";
    auto *handles = static_cast<const ANARIObject *>(data);
    for (size_t i = 0; i < bytes / sizeof(ANARIObject); i++)
      out += (i ? " " : "") + name(handles[i]);
    return out + "]";
  }

  template <typename T>
  T makeArray(const char *call,
      ANARIDataType arrayType,
      const void *memory,
      ANARIMemoryDeleter deleter,
      const void *userData,
      ANARIDataType type,
      const std::string &counts,
      uint64_t items)
  {
    const size_t bytes = anari::sizeOf(type) * items;
    T handle = make<T>(arrayType,
        std::string(call) + " " + anari::toString(type) + " " + counts + " "
            + elementsText(memory, type, bytes));
    Object &a = *find(handle);
    a.memory = memory;
    a.deleter = deleter;
    a.deleterUserData = userData;
    a.elementType = type;
    a.bytes = bytes;
    return handle;
  }

  // Object creation //////////////////////////////////////////////////////////

  ANARIArray1D newArray1D(const void *memory,
      ANARIMemoryDeleter deleter,
      const void *userData,
      ANARIDataType type,
      uint64_t n1) override
  {
    return makeArray<ANARIArray1D>("newArray1D",
        ANARI_ARRAY1D,
        memory,
        deleter,
        userData,
        type,
        std::to_string(n1),
        n1);
  }

  ANARIArray2D newArray2D(const void *memory,
      ANARIMemoryDeleter deleter,
      const void *userData,
      ANARIDataType type,
      uint64_t n1,
      uint64_t n2) override
  {
    return makeArray<ANARIArray2D>("newArray2D",
        ANARI_ARRAY2D,
        memory,
        deleter,
        userData,
        type,
        std::to_string(n1) + "x" + std::to_string(n2),
        n1 * n2);
  }

  ANARIArray3D newArray3D(const void *memory,
      ANARIMemoryDeleter deleter,
      const void *userData,
      ANARIDataType type,
      uint64_t n1,
      uint64_t n2,
      uint64_t n3) override
  {
    return makeArray<ANARIArray3D>("newArray3D",
        ANARI_ARRAY3D,
        memory,
        deleter,
        userData,
        type,
        std::to_string(n1) + "x" + std::to_string(n2) + "x"
            + std::to_string(n3),
        n1 * n2 * n3);
  }

  ANARIGeometry newGeometry(const char *t) override
  {
    return makeTyped<ANARIGeometry>(ANARI_GEOMETRY, "newGeometry", t);
  }
  ANARIMaterial newMaterial(const char *t) override
  {
    return makeTyped<ANARIMaterial>(ANARI_MATERIAL, "newMaterial", t);
  }
  ANARISampler newSampler(const char *t) override
  {
    return makeTyped<ANARISampler>(ANARI_SAMPLER, "newSampler", t);
  }
  ANARISurface newSurface() override
  {
    return make<ANARISurface>(ANARI_SURFACE, "newSurface");
  }
  ANARISpatialField newSpatialField(const char *t) override
  {
    return makeTyped<ANARISpatialField>(
        ANARI_SPATIAL_FIELD, "newSpatialField", t);
  }
  ANARIVolume newVolume(const char *t) override
  {
    return makeTyped<ANARIVolume>(ANARI_VOLUME, "newVolume", t);
  }
  ANARILight newLight(const char *t) override
  {
    return makeTyped<ANARILight>(ANARI_LIGHT, "newLight", t);
  }
  ANARIGroup newGroup() override
  {
    return make<ANARIGroup>(ANARI_GROUP, "newGroup");
  }
  ANARIInstance newInstance(const char *t) override
  {
    return makeTyped<ANARIInstance>(ANARI_INSTANCE, "newInstance", t);
  }
  ANARIWorld newWorld() override
  {
    return make<ANARIWorld>(ANARI_WORLD, "newWorld");
  }
  ANARICamera newCamera(const char *t) override
  {
    return makeTyped<ANARICamera>(ANARI_CAMERA, "newCamera", t);
  }
  ANARIRenderer newRenderer(const char *t) override
  {
    return makeTyped<ANARIRenderer>(ANARI_RENDERER, "newRenderer", t);
  }
  ANARIFrame newFrame() override
  {
    return make<ANARIFrame>(ANARI_FRAME, "newFrame");
  }
  ANARIObject newObject(const char *objectType, const char *t) override
  {
    return make<ANARIObject>(
        ANARI_OBJECT, "newObject " + quoted(objectType) + " " + quoted(t));
  }

  // Parameters ///////////////////////////////////////////////////////////////

  void setParameter(ANARIObject object,
      const char *param,
      ANARIDataType type,
      const void *mem) override
  {
    Object *o = find(object);
    const std::string p = param;
    if (o && p == "frameCompletionCallback") {
      o->callback = *static_cast<const ANARIFrameCompletionCallback *>(mem);
      return;
    }
    if (o && p == "frameCompletionCallbackUserData") {
      o->callbackUserData = mem;
      return;
    }

    std::string value;
    if (anari::isObject(type))
      value = name(*static_cast<const ANARIObject *>(mem));
    else if (type == ANARI_STRING)
      value = quoted(static_cast<const char *>(mem));
    else if (type == ANARI_STRING_LIST) {
      value = "[";
      auto *list = static_cast<const char *const *>(mem);
      for (size_t i = 0; list[i]; i++)
        value += (i ? " " : "") + quoted(list[i]);
      value += "]";
    } else
      value = bytesText(mem, anari::sizeOf(type));
    log("setParameter " + name(object) + " " + quoted(param) + " "
        + anari::toString(type) + " " + value);

    if (o && p == "size" && type == ANARI_UINT32_VEC2)
      std::memcpy(o->size, mem, sizeof(o->size));
    if (p == "emitStatus" && type == ANARI_STRING) {
      status(
          object, o ? o->type : ANARI_DEVICE, static_cast<const char *>(mem));
    }
  }

  void unsetParameter(ANARIObject object, const char *param) override
  {
    log("unsetParameter " + name(object) + " " + quoted(param));
  }

  void unsetAllParameters(ANARIObject object) override
  {
    log("unsetAllParameters " + name(object));
  }

  void *mapParameterArray1D(
      ANARIObject, const char *, ANARIDataType, uint64_t, uint64_t *) override
  {
    log("mapParameterArray1D");
    return nullptr;
  }
  void *mapParameterArray2D(ANARIObject,
      const char *,
      ANARIDataType,
      uint64_t,
      uint64_t,
      uint64_t *) override
  {
    log("mapParameterArray2D");
    return nullptr;
  }
  void *mapParameterArray3D(ANARIObject,
      const char *,
      ANARIDataType,
      uint64_t,
      uint64_t,
      uint64_t,
      uint64_t *) override
  {
    log("mapParameterArray3D");
    return nullptr;
  }
  void unmapParameterArray(ANARIObject, const char *) override
  {
    log("unmapParameterArray");
  }

  void commitParameters(ANARIObject object) override
  {
    log("commitParameters " + name(object));
  }

  void release(ANARIObject object) override
  {
    log("release " + name(object));
    if (handleIsDevice(object)) {
      if (--m_references == 0)
        delete this;
      return;
    }
    Object *o = find(object);
    if (!o || --o->references > 0)
      return;
    if (o->deleter)
      o->deleter(o->deleterUserData, o->memory);
    m_objects.erase(uintptr_t(object));
  }

  void retain(ANARIObject object) override
  {
    log("retain " + name(object));
    if (handleIsDevice(object))
      m_references++;
    else if (Object *o = find(object))
      o->references++;
  }

  void *mapArray(ANARIArray array) override
  {
    Object *a = find(array);
    return a ? const_cast<void *>(a->memory) : nullptr;
  }

  void unmapArray(ANARIArray array) override
  {
    Object *a = find(array);
    log("unmapArray " + name(array) + " "
        + (a ? elementsText(a->memory, a->elementType, a->bytes) : "?"));
  }

  // Queries //////////////////////////////////////////////////////////////////

  int getProperty(ANARIObject object,
      const char *prop,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask) override
  {
    int found = 0;
    const std::string p = prop;
    if (p == "answer" && type == ANARI_INT32 && size >= sizeof(int32_t)) {
      const int32_t answer = 42;
      std::memcpy(mem, &answer, sizeof(answer));
      found = 1;
    } else if (p == "bounds" && type == ANARI_FLOAT32_BOX3
        && size >= 6 * sizeof(float)) {
      const float bounds[] = {1, 2, 3, 4, 5, 6};
      std::memcpy(mem, bounds, sizeof(bounds));
      found = 1;
    } else if (p == "extension" && type == ANARI_STRING_LIST
        && size >= sizeof(void *)) {
      const char *const *list = EXTENSIONS;
      std::memcpy(mem, &list, sizeof(list));
      found = 1;
    }
    log("getProperty " + name(object) + " " + quoted(prop) + " "
        + anari::toString(type) + " size=" + std::to_string(size) + " wait="
        + std::to_string(int(mask)) + " -> " + std::to_string(found));
    return found;
  }

  const char **getObjectSubtypes(ANARIDataType type) override
  {
    log(std::string("getObjectSubtypes ") + anari::toString(type));
    static const char *renderers[] = {"default", "fancy", nullptr};
    return type == ANARI_RENDERER ? renderers : nullptr;
  }

  const void *getObjectInfo(ANARIDataType type,
      const char *subtype,
      const char *info,
      ANARIDataType infoType) override
  {
    log(std::string("getObjectInfo ") + anari::toString(type) + " "
        + quoted(subtype) + " " + quoted(info) + " "
        + anari::toString(infoType));
    const std::string i = info ? info : "";
    if (i == "description" && infoType == ANARI_STRING)
      return intern("a " + std::string(subtype ? subtype : "null") + " thing");
    if (i == "extension" && infoType == ANARI_STRING_LIST)
      return EXTENSIONS;
    if (i == "parameter" && infoType == ANARI_PARAMETER_LIST) {
      static const ANARIParameter parameters[] = {{"color", ANARI_FLOAT32_VEC4},
          {"radius", ANARI_FLOAT32},
          {nullptr, ANARI_UNKNOWN}};
      return parameters;
    }
    if (i == "types" && infoType == ANARI_DATA_TYPE_LIST) {
      static const ANARIDataType types[] = {
          ANARI_FLOAT32, ANARI_UINT8, ANARI_UNKNOWN};
      return types;
    }
    return nullptr;
  }

  const void *getParameterInfo(ANARIDataType type,
      const char *subtype,
      const char *param,
      ANARIDataType paramType,
      const char *info,
      ANARIDataType infoType) override
  {
    log(std::string("getParameterInfo ") + anari::toString(type) + " "
        + quoted(subtype) + " " + quoted(param) + " "
        + anari::toString(paramType) + " " + quoted(info) + " "
        + anari::toString(infoType));
    const std::string i = info ? info : "";
    if (i == "default" && infoType == ANARI_FLOAT32) {
      static const float value = 0.5f;
      return &value;
    }
    if (i == "description" && infoType == ANARI_STRING) {
      return intern(std::string(param ? param : "null") + " of "
          + (subtype ? subtype : "null"));
    }
    return nullptr;
  }

  // Frames ///////////////////////////////////////////////////////////////////

  // Pixels are a fixed pattern that changes with every render.
  const void *frameBufferMap(ANARIFrame frame,
      const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override
  {
    log("frameBufferMap " + name(frame) + " " + quoted(channel));
    Object *f = find(frame);
    const std::string c = channel;
    *width = *height = 0;
    *pixelType = ANARI_UNKNOWN;
    if (!f)
      return nullptr;
    // Device memory the host could never read: mapping this through the guard
    // device must not get this far.
    if (c == "channel.colorCUDA")
      return reinterpret_cast<const void *>(uintptr_t(16));
    const size_t pixels = size_t(f->size[0]) * f->size[1];
    if (c == "channel.color") {
      f->color.resize(pixels * 4);
      for (size_t i = 0; i < f->color.size(); i++)
        f->color[i] = uint8_t(i * 31 + 7 * f->renders);
      *pixelType = ANARI_UFIXED8_VEC4;
    } else if (c == "channel.depth") {
      f->depth.resize(pixels);
      for (size_t i = 0; i < pixels; i++)
        f->depth[i] = 0.5f * float(i) + float(f->renders);
      *pixelType = ANARI_FLOAT32;
    } else
      return nullptr;
    *width = f->size[0];
    *height = f->size[1];
    return c == "channel.color" ? static_cast<const void *>(f->color.data())
                                : static_cast<const void *>(f->depth.data());
  }

  void frameBufferUnmap(ANARIFrame frame, const char *channel) override
  {
    log("frameBufferUnmap " + name(frame) + " " + quoted(channel));
  }

  void renderFrame(ANARIFrame frame) override
  {
    log("renderFrame " + name(frame));
    if (Object *f = find(frame)) {
      f->renders++;
      if (m_slow || m_late)
        m_ready = std::chrono::steady_clock::now() + SLOW_RENDER;
      if (!m_slow && f->callback)
        f->callback(f->callbackUserData, this_device(), frame);
    }
  }

  int frameReady(ANARIFrame, ANARIWaitMask mask) override
  {
    if (mask == ANARI_WAIT)
      std::this_thread::sleep_until(m_ready);
    return std::chrono::steady_clock::now() >= m_ready ? 1 : 0;
  }

  void discardFrame(ANARIFrame frame) override
  {
    log("discardFrame " + name(frame));
  }

 private:
  const char *intern(const std::string &s)
  {
    m_strings.push_back(std::make_unique<std::string>(s));
    return m_strings.back()->c_str();
  }

  FILE *m_log{nullptr};
  const bool m_slow;
  const bool m_late;
  std::chrono::steady_clock::time_point m_ready{}; // of the latest render
  uintptr_t m_last{0};
  uint32_t m_references{1};
  std::map<uintptr_t, Object> m_objects;
  std::vector<std::unique_ptr<std::string>> m_strings;
};

struct RecorderLibrary : public anari::LibraryImpl
{
  RecorderLibrary(void *lib, ANARIStatusCallback cb, const void *cbPtr)
      : anari::LibraryImpl(lib, cb, cbPtr)
  {}

  void status(const char *message)
  {
    if (auto cb = defaultStatusCB()) {
      cb(defaultStatusCBUserPtr(),
          nullptr,
          nullptr,
          ANARI_LIBRARY,
          ANARI_SEVERITY_WARNING,
          ANARI_STATUS_INVALID_OPERATION,
          message);
    }
  }

  // Every subtype is a recording device, except 'broken', which is none.
  ANARIDevice newDevice(const char *subtype) override
  {
    if (std::strcmp(subtype, "broken") == 0) {
      status("recorder: about to fail");
      return nullptr;
    }
    status("recorder: loading");
    return ANARIDevice(new Recorder(this_library(),
        std::strcmp(subtype, "slow") == 0,
        std::strcmp(subtype, "late") == 0));
  }

  const char **getDeviceExtensions(const char *subtype) override
  {
    return subtype && std::strcmp(subtype, "slow") == 0 ? SLOW_EXTENSIONS
                                                        : EXTENSIONS;
  }
};

} // namespace

extern "C" __attribute__((visibility("default")))
ANARI_DEFINE_LIBRARY_ENTRYPOINT(guard_recorder, handle, scb, scbPtr)
{
  return ANARILibrary(new RecorderLibrary(handle, scb, scbPtr));
}
