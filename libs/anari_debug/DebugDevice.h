// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari.h"
#include "anari/detail/Device.h"
#include "anari/detail/IntrusivePtr.h"
#include "anari/detail/Library.h"

#include "anari_variant.h"

// std
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <stack>
#include <ostream>
#include <iostream>


#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PRINT_BACKTRACES
#define MAX_TRACE_LENGTH 20
#include <execinfo.h>

#endif

struct ObjectData {
private:
  ANARIDataType type;
  ANARIObject object;
  int id = -1;
  std::string name;

  int public_refcount = 1;
  bool dirty = true;

  size_t logBegin = 0;
  size_t logEnd = 0;

  struct ParamData {
    AnariVariant data;
    size_t logBegin = 0;
    size_t logEnd = 0;
  };

  std::map<std::string, ParamData> params;
public:
  using param_map = std::map<std::string, ParamData>;
  using iterator = param_map::iterator;

  #ifdef PRINT_BACKTRACES
  void *trace_buffer[MAX_TRACE_LENGTH];
  int trace_length;
  #endif

  bool isDirty() const { return dirty; }
  void setDirty(bool value) { dirty = value; }

  ANARIDataType getType() const { return type; }
  ANARIObject getObject() const { return object; }

  void setName(const std::string &n) { name = n; }
  const std::string& getName() const { return name; }

  void setParam(const char *name, ANARIDataType type, const void *mem, size_t logBegin, size_t logEnd) {
    params[name].data.set(type, mem);
    params[name].logBegin = logBegin;
    params[name].logEnd = logEnd;
    dirty = true;
  }

  void removeParam(const char *name) {
    params.erase(name);
    dirty = true;
  }

  bool getParam(const char *name, ANARIDataType type, void *mem) {
    auto iter = params.find(name);
    if(iter != params.end()) {
      return iter->second.data.get(type, mem);
    } else {
      return false;
    }
  }

  void commit(size_t begin, size_t end) {
    logBegin = begin;
    logEnd = end;
    dirty = false;
  }

  void retain() { public_refcount += 1; }
  void release() { public_refcount -= 1; }
  int getRefcount() const { return public_refcount; }

  const param_map& getParamMap() { return params; }

  std::vector<ANARIObject> ownedArray;
  char *externalArray = 0;
  ANARIDataType arrayType = ANARI_UNKNOWN;
  uint64_t arrayStrides[3];
  uint64_t arrayItems[3];
  void *mappedPointer;

  ObjectData(ANARIDataType t, ANARIObject o, int i);

/*
  ObjectData(const ObjectData&) = delete;
  ObjectData& operator= (const ObjectData&) = delete;
*/

};

struct LoggedStatus {
  bool fromDebug;
  ANARIStatusCode error;
  std::string message;
  LoggedStatus(bool dbg, std::string msg, ANARIStatusCode err)
    : fromDebug(dbg), error(err), message(msg)
    {}
};

struct ScopeBase {
  virtual const char* getName() const = 0;
  int logBegin = 0;
  int logEnd = 0;
};

struct DebugDevice : public anari::Device, public anari::RefCounted
{
private:
  ANARIStatusCallback statusFunc = nullptr;
  void *statusUserData = nullptr;
  static void statusCallbackWrapper(
    void* userPtr,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char* message);

  std::stack<ScopeBase*> scopes;

  std::unique_ptr<anari::Device> forward;

  bool wrapHandles = true;
  std::unordered_map<ANARIObject, ObjectData> objectMap;
  std::unordered_map<ANARIObject, ANARIObject> reverseObjectMap;
  int nextObjectID = 1;
  int nextArgumentID = 1;

  std::unordered_map<const void*, int> pointerMap;

  std::unique_ptr<std::ostream> captureStream;
  std::string tracedir;

  std::vector<LoggedStatus> statusLog;

  static int debugInstanceCount;
  int debugInstanceID;

public:
  bool print_backtraces = false;

  DebugDevice(Device *f);

  void pushScope(ScopeBase *s);

  void popScope();

  std::ostream* getCaptureStream();

  void newPointer(const void *ptr);
  std::string getPointerName(const void *ptr);

  ANARIObject newObjectGeneric(ANARIObject obj, ANARIDataType type);
  ANARIObject unwrapObjectGeneric(ANARIObject obj);
  ANARIObject wrapObjectGeneric(ANARIObject obj);

  template<typename T>
  T wrapNewObject(T obj, ANARIDataType type = anari::ANARITypeFor<T>::value) {
    return static_cast<T>(newObjectGeneric(obj, type));
  }

  template<typename T>
  T unwrapObject(T obj) {
    return static_cast<T>(unwrapObjectGeneric(obj));
  }

  template<typename T>
  T wrapObject(T obj) {
    return static_cast<T>(wrapObjectGeneric(obj));
  }

  ObjectData* getObject(ANARIObject obj);

  const std::string& getObjectName(ANARIObject obj);

  void insertStatusMessage(const char *message);

  void nameObject(ANARIObject obj, const char *name);

  void reportStatus(const char *format, ...);

  void reportError(ANARIStatusCode error, const char *format, ...);

  size_t logMessage(bool dbg, std::string message, ANARIStatusCode error = ANARI_STATUS_NO_ERROR);
  size_t logPos() const;

  int deviceImplements(const char *extension) override;

  void deviceSetParameter(
      const char *id, ANARIDataType type, const void *mem) override;

  void deviceUnsetParameter(const char *id);

  void deviceCommit() override;

  void deviceRetain() override;

  void deviceRelease() override;

// Data Arrays //////////////////////////////////////////////////////////////

  ANARIArray1D newArray1D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t byteStride1);

  ANARIArray2D newArray2D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t byteStride1,
      uint64_t byteStride2);

  ANARIArray3D newArray3D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3,
      uint64_t byteStride1,
      uint64_t byteStride2,
      uint64_t byteStride3);

  void *mapArray(ANARIArray);
  void unmapArray(ANARIArray);

  void arrayRangeUpdated1D(
      ANARIArray1D, uint64_t startIndex1, uint64_t elementCount1);

  void arrayRangeUpdated2D(ANARIArray2D,
      uint64_t startIndex1,
      uint64_t startIndex2,
      uint64_t elementCount1,
      uint64_t elementCount2);

  void arrayRangeUpdated3D(ANARIArray3D,
      uint64_t startIndex1,
      uint64_t startIndex2,
      uint64_t startIndex3,
      uint64_t elementCount1,
      uint64_t elementCount2,
      uint64_t elementCount3);

  // Renderable Objects ///////////////////////////////////////////////////////

  ANARILight newLight(const char *type);

  ANARICamera newCamera(const char *type);

  ANARIGeometry newGeometry(const char *type);
  ANARISpatialField newSpatialField(const char *type);

  ANARISurface newSurface();
  ANARIVolume newVolume(const char *type);

  // Model Meta-Data //////////////////////////////////////////////////////////

  ANARIMaterial newMaterial(const char *material_type);

  ANARISampler newSampler(const char *type);

  // Instancing ///////////////////////////////////////////////////////////////

  ANARIGroup newGroup();

  ANARIInstance newInstance();

  // Top-level Worlds /////////////////////////////////////////////////////////

  ANARIWorld newWorld();

  // Object + Parameter Lifetime Management ///////////////////////////////////

  void setParameter(ANARIObject object,
      const char *name,
      ANARIDataType type,
      const void *mem);

  void unsetParameter(ANARIObject object, const char *name);

  void commit(ANARIObject object);

  void release(ANARIObject _obj);
  void retain(ANARIObject _obj);


  // Object Query Interface ///////////////////////////////////////////////////

  int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask);

  // FrameBuffer Manipulation /////////////////////////////////////////////////

  ANARIFrame frameCreate();

  const void *frameBufferMap(ANARIFrame fb, const char *channel);

  void frameBufferUnmap(ANARIFrame fb, const char *channel);

  // Frame Rendering //////////////////////////////////////////////////////////

  ANARIRenderer newRenderer(const char *type);

  void renderFrame(ANARIFrame) override;
  int frameReady(ANARIFrame, ANARIWaitMask) override;
  void discardFrame(ANARIFrame) override;

  ANARIObject newObject(const char *type, const char *subtype);

  void (*getProcAddress(const char *name))(void);

  ~DebugDevice();

};
