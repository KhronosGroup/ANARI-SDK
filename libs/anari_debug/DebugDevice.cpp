#include "DebugDevice.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include <tuple>
#include <vector>
#include <algorithm>
#include <utility>
#include <type_traits>

#include <iostream>
#include <ostream>
#include <sstream>
#include <fstream>
#include <limits>

#include <time.h>

#include "traits.h"

#include "anari_variant.h"
#include "anari_type_printing.h"

#include "anari/ext/anari_ext_interface.h"

// object specific info storage
ObjectData::ObjectData(ANARIDataType t, ANARIObject o, int i)
  : type(t), object(o), id(i), name(anari_type_varname(type) + std::to_string(i))
{
}

// utilities
template<int i, int end>
struct apply_t {
  apply_t<i+1, end> next;
  template<typename F, typename T>
  void operator()(F &f, const T &t) const {
    f(i, std::get<i>(t));
    next(f, t);
  }
};

template<int end>
struct apply_t<end, end> {
  template<typename F, typename T>
  void operator()(F &f, const T &t) const { (void)f; (void)t; }
};

template<typename F, typename ...T>
void apply(F &f, const std::tuple<T...> &t)  {
  apply_t<0, sizeof...(T)> first;
  first(f, t);
}


struct validate_t {
  DebugDevice *dd;
  validate_t(DebugDevice *d) : dd(d) {}

  template<class T>
  void operator()(int i, const T &t) const {
    (void)i;
    validation_trait<T>::validate(dd, t);
  }
};


struct print_t {
  DebugDevice *dd;
  std::ostream &out;
  print_t(DebugDevice *d, std::ostream &o) : dd(d), out(o) {}

  template<class T, typename std::enable_if<!isObject(anari::ANARITypeFor<T>::value) && !std::is_pointer<T>::value, int>::type = 0>
  void operator()(int i, const T &val) const {
    (void)i;
    out << ", " << val;
  }

  template<class T, typename std::enable_if<isObject(anari::ANARITypeFor<T>::value), int>::type = 0 >
  void operator()(int i, const T &val) const {
    (void)i;
    out << ", " << dd->getObjectName(val);
  }

  template<class T, typename std::enable_if<!isObject(anari::ANARITypeFor<T>::value) && std::is_pointer<T>::value, int>::type = 0 >
  void operator()(int i, const T &val) const {
    (void)i;
    if(std::is_same<T, const char*>::value) {
      out << ", \"" << val << "\"";
    } else {
      out << ", " << dd->getPointerName(reinterpret_cast<const void*>(val));
    }
  }
};

// this is a workaround for not being able to declare a void member
template<class T>
struct empty_if_void { using type = T; };

template<>
struct empty_if_void<void> { struct type { }; };


template<class T, typename std::enable_if<isObject(anari::ANARITypeFor<T>::value), int>::type = 0>
void declare_if_object(std::ostream &out, DebugDevice *dd, const T &val) {
  ANARIDataType type = anari::ANARITypeFor<T>::value;
  if(ObjectData *data = dd->getObject(val)) {
    out << anari_type_name(type) << ' ' << data->getName() << " = ";
  }
}

template<class T, typename std::enable_if<!isObject(anari::ANARITypeFor<T>::value), int>::type = 0>
void declare_if_object(std::ostream &, DebugDevice *, const T &) {
  // not an object so do nothing
}

// the scope objects wraps the contents of the function call
template<typename R, typename ...Args>
struct Scope : public ScopeBase {
  R (*function)(ANARIDevice, Args...);
  std::tuple<Args...> arguments;

  using return_type = R;
  typename empty_if_void<return_type>::type return_value = { };

  const char *name;
  DebugDevice *dd;

  bool captured = false;
  bool has_report = false;

  Scope(DebugDevice *d, const char* n,  R (*f)(ANARIDevice, Args...), Args... args)
    : function(f), arguments(args...), name(n), dd(d) {

    dd->pushScope(this);

    validate_t v(dd);
    ::apply(v, arguments);
  }

  const char *getName() const { return name; }

  void capture() {
    if(std::ostream *out = dd->getCaptureStream()) {
      declare_if_object(*out, dd, return_value);
      *out << name << "(device";
      print_t p(dd, *out);
      ::apply(p, arguments);
      *out  << ");" << std::endl;
    }
    captured = true;
  }

  ~Scope() {
    if(!captured) {
      capture();
    }
    dd->popScope();

#ifdef PRINT_BACKTRACES
    if(dd->print_backtraces && logEnd>logBegin) {
      void *trace_buffer[MAX_TRACE_LENGTH];
      int size = backtrace(trace_buffer, MAX_TRACE_LENGTH);
      char **symbols = backtrace_symbols(trace_buffer, size);
      dd->reportStatus("in %s", name);

      // start at 2 to skip the debug layer internals
      for(int i = 2;i<size;++i) {
        dd->reportStatus("   %s ", symbols[i]);
      }
      free(symbols);
    }
#endif
  }
};

template<typename R, typename ...Args>
Scope<R, Args...> make_scope(DebugDevice *dd, const char *name, R (*f)(ANARIDevice, Args...), Args... args) {
  return Scope<R, Args...>(dd, name, f, args...);
}

#define MAKE_SCOPE(N, ...) auto scope = make_scope(this, #N , N, __VA_ARGS__ );
#define MAKE_SCOPE_NO_ARGS(N) auto scope = make_scope(this, #N , N);

#define SCOPE_RETURN( X ) scope.return_value = X; return scope.return_value;


int DebugDevice::debugInstanceCount = 0;

DebugDevice::DebugDevice(Device *f) : forward(f) {
  // setup tracing

  debugInstanceID = debugInstanceCount++;

  const char *dir = std::getenv("ANARI_DEBUG_TRACE_DIR");
  if(dir) {
    tracedir = dir;
#if defined(_WIN32)
    tracedir += "_" + std::to_string(time(NULL)) + "_" + std::to_string(debugInstanceID);
    _mkdir(tracedir.c_str());
#else
    tracedir += "_" + std::to_string(time(NULL)) + "_" + std::to_string(getpid()) + "_" + std::to_string(debugInstanceID);
    mkdir(tracedir.c_str(), 0777);
#endif
    captureStream.reset(new std::ofstream(tracedir + "/trace.c"));
    reportStatus("trace directory: %s", tracedir.c_str());
  }

  const char *backtraces = std::getenv("ANARI_DEBUG_PRINT_BACKTRACE");
  if(backtraces) {
    print_backtraces = std::strtol(backtraces, nullptr, 10);
  }

  auto entry = objectMap.emplace(std::make_pair(nullptr, ObjectData(ANARI_OBJECT, nullptr, 0)));
  reverseObjectMap[nullptr] = nullptr;
  // give the null handle the max possible reference count so no one "frees" it?
  // entry.first->second.public_refcount = std::numeric_limits<int>::max();
  entry.first->second.setName("NULL_HANDLE");

  // insert the device handle
  {
    int id = nextObjectID++;
    ANARIObject obj = (ANARIObject)this;
    ANARIObject handle = (ANARIObject)forward.get();
    auto inserted = objectMap.emplace(std::make_pair(handle, ObjectData(ANARI_DEVICE, obj, id)));
    inserted.first->second.setName("device");
    reverseObjectMap[obj] = handle;
  }

#if 0 // TODO: figure out status callback forwarding
  forward->m_defaultStatusCB = DebugDevice::statusCallbackWrapper;
  //forward->deviceSetParam("statusCallbackPtr", ANARI_VOID_POINTER, this);
#endif
}

// the api probably shouldn't be reentrant but just to be sure
void DebugDevice::pushScope(ScopeBase *s) {
  scopes.push(s);
  s->logBegin = logPos();
}

void DebugDevice::popScope() {
  ScopeBase *s = scopes.top();
  s->logEnd = logPos();
  scopes.pop();
}

std::ostream* DebugDevice::getCaptureStream() {
  return captureStream.get();
}

void DebugDevice::newPointer(const void *ptr) {
  auto iter = pointerMap.find(ptr);
  if(iter!= pointerMap.end()) {
    iter->second += 1;
  } else {
    pointerMap.emplace(ptr, 0);
  }
}

std::string DebugDevice::getPointerName(const void *ptr) {
  auto iter = pointerMap.find(ptr);
  if(iter!= pointerMap.end()) {
    std::stringstream name;
    name << "ptr" << ptr << "_" << iter->second;
    return name.str();
  } else {
    return "unknown_ptr";
  }
}


// registers a new object
ANARIObject DebugDevice::newObjectGeneric(ANARIObject obj, ANARIDataType type) {
  int id = nextObjectID++;
  ANARIObject handle = obj;

  if(wrapHandles) {
    handle = ANARIObject(intptr_t(id));
  }

  auto iter = objectMap.find(handle);
  if(iter != objectMap.end()) {
    // object already exists?
    // dont remove the null object
    if(iter->first != nullptr) {
      objectMap.erase(iter);
    }
  }

  auto inserted = objectMap.emplace(std::make_pair(handle, ObjectData(type, obj, id)));
  reverseObjectMap[obj] = handle;

  #ifdef PRINT_BACKTRACES
  if(print_backtraces) {
    inserted.first->second.trace_length = backtrace(inserted.first->second.trace_buffer, MAX_TRACE_LENGTH);
  }
  #endif

  return handle;
}

ANARIObject DebugDevice::unwrapObjectGeneric(ANARIObject obj) {
  if(obj == (ANARIObject)this) {
    return (ANARIObject)forward.get();
  }
  if(wrapHandles) {
    auto iter = objectMap.find(obj);
    if(iter != objectMap.end() && iter->first != nullptr) {
      return iter->second.getObject();
    } else {
      return nullptr;
    }
  } else {
    return obj;
  }
}

ANARIObject DebugDevice::wrapObjectGeneric(ANARIObject obj) {
  if(obj == (ANARIObject)forward.get()) {
    return (ANARIObject)this;
  }
  if(wrapHandles) {
    auto iter = reverseObjectMap.find(obj);
    if(iter != reverseObjectMap.end()) {
      //reportStatus("wrapped %p to %p", obj, iter->second);
      return iter->second;
    } else {
      return nullptr;
    }
  } else {
    return obj;
  }
}


// obtain a registered object
ObjectData* DebugDevice::getObject(ANARIObject obj) {
  auto iter = objectMap.find(obj);
  if(iter != objectMap.end()) {
    return &iter->second;
  } else {
    return nullptr;
  }
}

const std::string& DebugDevice::getObjectName(ANARIObject obj) {
  static const std::string unknown("unknown");
  auto iter = objectMap.find(obj);
  if(iter != objectMap.end()) {
    return iter->second.getName();
  } else {
    return unknown;
  }
}

void DebugDevice::insertStatusMessage(const char *message) {
  reportStatus("%s", message);
}

void DebugDevice::nameObject(ANARIObject obj, const char *name) {
  ObjectData *data = getObject(obj);

  if(std::ostream *pout = getCaptureStream()) {
    std::ostream &out = *pout;
    out << anari_type_name(data->getType()) << ' ' << name << " = " << data->getName() << ";\n";
  }

  data->setName(name);
}

size_t DebugDevice::logMessage(bool dbg, std::string message, ANARIStatusCode error) {
  int count = statusLog.size();
  statusLog.emplace_back(dbg, message, error);
  return count;
}

size_t DebugDevice::logPos() const {
  return statusLog.size();
}

// conveneince functions for formatting output to status and error functions
static const char message_prefix[] = "[VALIDATION] ";

void DebugDevice::reportStatus(const char *format, ...) {
  va_list arglist;
  va_list arglist_copy;
  va_start(arglist, format);
  va_copy(arglist_copy, arglist);
  int count = std::vsnprintf(nullptr, 0, format, arglist);
  va_end(arglist);

  size_t prefix_length = sizeof(message_prefix) - 1;
  std::vector<char> message(prefix_length + count + 1);
  std::copy(message_prefix, message_prefix+prefix_length, message.data());

  va_start(arglist_copy, format);
  std::vsnprintf(message.data()+prefix_length, count + 1, format, arglist_copy);
  va_end(arglist_copy);

  if(std::ostream *pout = getCaptureStream()) {
    *pout << "// " << message.data() << std::endl;
  }
  logMessage(true, message.data());
  if (statusFunc != nullptr) {
    statusFunc(statusUserData, (ANARIDevice)this, nullptr, ANARI_UNKNOWN, ANARI_SEVERITY_INFO, ANARI_STATUS_NO_ERROR, message.data());
  }
}

void DebugDevice::reportError(ANARIStatusCode error, const char *format, ...) {
  va_list arglist;
  va_list arglist_copy;
  va_start(arglist, format);
  va_copy(arglist_copy, arglist);
  int count = std::vsnprintf(nullptr, 0, format, arglist);
  va_end(arglist);

  size_t prefix_length = sizeof(message_prefix) - 1;
  std::vector<char> message(prefix_length + count + 1);
  std::copy(message_prefix, message_prefix+prefix_length, message.data());

  va_start(arglist_copy, format);
  std::vsnprintf(message.data()+prefix_length, count + 1, format, arglist_copy);
  va_end(arglist_copy);

  if(std::ostream *pout = getCaptureStream()) {
    *pout << "// " << message.data() << std::endl;
  }
  logMessage(true, message.data(), error);

  if (statusFunc != nullptr) {
    statusFunc(statusUserData, (ANARIDevice)this, nullptr, ANARI_UNKNOWN, ANARI_SEVERITY_ERROR, error, message.data());

  }
}

int DebugDevice::deviceImplements(const char *_extension)
{
  MAKE_SCOPE(anariDeviceImplements, _extension)
  std::string extension = _extension;
  int result = 0;
  if(extension == ANARI_KHR_FRAME_COMPLETION_CALLBACK) {
    result = 0;
  } else {
    result = forward->deviceImplements(_extension);
  }

  SCOPE_RETURN(result)
}

void DebugDevice::deviceSetParameter(
    const char *id, ANARIDataType type, const void *mem) {
  MAKE_SCOPE(anariSetParameter, (ANARIObject)this, id, type, mem)

  forward->deviceSetParameter(id, type, mem);
}

void DebugDevice::deviceUnsetParameter(const char *id) {
  MAKE_SCOPE(anariUnsetParameter, (ANARIObject)this, id)

  forward->deviceUnsetParameter(id);
}

void DebugDevice::deviceCommit() {
  ANARIObject object = (ANARIObject)forward.get();
  MAKE_SCOPE(anariCommit, object)

  forward->deviceCommit();
}

void DebugDevice::deviceRetain()
{
  this->refInc();
}

void DebugDevice::deviceRelease()
{
  this->refDec();
}

void DebugDevice::statusCallbackWrapper(
    void* userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char* statusDetails) {
  DebugDevice *dd = reinterpret_cast<DebugDevice*>(userData);

  if(dd->statusFunc != nullptr) {
    dd->statusFunc(
      dd->statusUserData,
      (ANARIDevice)dd,
      dd->wrapObjectGeneric(source),
      sourceType,
      severity,
      code,
      statusDetails);
  }

  if (severity == ANARI_SEVERITY_FATAL_ERROR||severity == ANARI_SEVERITY_FATAL_ERROR) {
    dd->logMessage(false, statusDetails, code);
  } else {
    dd->logMessage(false, statusDetails);
  }

  if(std::ostream *pout = dd->getCaptureStream()) {
    if (severity == ANARI_SEVERITY_FATAL_ERROR) {
      *pout << "// [DEVICE FATAL] ";
    } else if (severity == ANARI_SEVERITY_ERROR) {
      *pout << "// [DEVICE ERROR] ";
    } else if (severity == ANARI_SEVERITY_WARNING) {
      *pout << "// [DEVICE WARNING] ";
    } else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
      *pout << "// [DEVICE PERFORMANCE] ";
    } else if (severity == ANARI_SEVERITY_INFO) {
      *pout << "// [DEVICE INFO] ";
    }
    *pout << statusDetails << std::endl;
  }
}

// Data Arrays //////////////////////////////////////////////////////////////

ANARIArray1D DebugDevice::newArray1D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType type,
      uint64_t numItems1,
      uint64_t byteStride1) {
  MAKE_SCOPE(anariNewArray1D, appMemory, deleter, userdata, type,
    numItems1, byteStride1)

  void *forwardedMemory = appMemory;
  uint64_t forwardedStride = byteStride1;

  // find canonical strides
  uint64_t elementSize = anari_type_size(type);
  uint64_t sourceStride1 = byteStride1 > 0 ? byteStride1 : elementSize;
  uint64_t sourceStride2 = sourceStride1;
  uint64_t sourceStride3 = sourceStride1;

  // total size in bytes
  uint64_t totalSize = numItems1*sourceStride1;

  // wrap objects handles if required
  std::vector<ANARIObject> unwrappedObjects;
  if(isObject(type) && wrapHandles) {
    unwrappedObjects.resize(numItems1);
    if(char *raw_objects = static_cast<char*>(appMemory)) {
      for(uint64_t i = 0;i<numItems1;++i) {
        char *itemPtr = raw_objects + i*sourceStride1;
        ANARIObject *wrapped = reinterpret_cast<ANARIObject*>(itemPtr);
        unwrappedObjects[i] = unwrapObject(*wrapped);
      }
    }

    forwardedMemory = unwrappedObjects.data();
    forwardedStride = 0;
  }

  ANARIArray1D result = forward->newArray1D(
      forwardedMemory,
      deleter,
      userdata,
      type,
      numItems1,
      forwardedStride);

  result = wrapNewObject(result, ANARI_ARRAY1D);

  ObjectData *obj = getObject(result);
  if(isObject(type) && wrapHandles) {
    obj->ownedArray = std::move(unwrappedObjects);
  }
  obj->externalArray = static_cast<char*>(appMemory);
  obj->arrayType = type;
  obj->arrayItems[0] = numItems1;
  obj->arrayItems[1] = 1;
  obj->arrayItems[2] = 1;
  obj->arrayStrides[0] = sourceStride1;
  obj->arrayStrides[1] = sourceStride2;
  obj->arrayStrides[2] = sourceStride3;


  newPointer(appMemory);
/*
  // capture data in file
  if(std::ostream *pout = getCaptureStream()) {
    std::ostream &out = *pout;

    const std::string ptrname = getPointerName(appMemory);

    // inline array if it contains objects or is small enough
    if(isObject(type) || numItems1 <= 32) {
      anari_type_array_definition(out, type, numItems1, appMemory, ptrname.c_str(), this);
    } else { // dump to file
      std::ofstream outfile(tracedir+"/" + ptrname + ".bin", std::ios_base::out | std::ios_base::binary);
      outfile.write(static_cast<const char*>(appMemory), totalSize);
      out << "void* " << ptrname << " = loadDataFromFile(\"" << ptrname << ".bin" << "\", " << totalSize << ");\n";
    }
  }
*/
  SCOPE_RETURN(result)
}

ANARIArray2D DebugDevice::newArray2D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType type,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t byteStride1,
      uint64_t byteStride2) {
  MAKE_SCOPE(anariNewArray2D, appMemory, deleter, userdata, type,
      numItems1, numItems2,
      byteStride1, byteStride2)

  ANARIArray2D result = forward->newArray2D(
      appMemory,
      deleter,
      userdata,
      type,
      numItems1,
      numItems2,
      byteStride1,
      byteStride2);
    result = wrapNewObject(result, ANARI_ARRAY2D);

    // find canonical strides
    uint64_t elementSize = anari_type_size(type);
    uint64_t sourceStride1 = byteStride1 > 0 ? byteStride1 : elementSize;
    uint64_t sourceStride2 = byteStride2 > 0 ? byteStride2 : sourceStride1*numItems1;
    uint64_t sourceStride3 = sourceStride2;

    ObjectData *obj = getObject(result);
    obj->externalArray = static_cast<char*>(appMemory);
    obj->arrayType = type;
    obj->arrayItems[0] = numItems1;
    obj->arrayItems[1] = numItems2;
    obj->arrayItems[2] = 1;
    obj->arrayStrides[0] = sourceStride1;
    obj->arrayStrides[1] = sourceStride2;
    obj->arrayStrides[2] = sourceStride3;

    SCOPE_RETURN(result)
}

ANARIArray3D DebugDevice::newArray3D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType type,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3,
      uint64_t byteStride1,
      uint64_t byteStride2,
      uint64_t byteStride3) {
  MAKE_SCOPE(anariNewArray3D, appMemory, deleter, userdata, type,
      numItems1, numItems2, numItems3,
      byteStride1, byteStride2, byteStride3)

  ANARIArray3D result = forward->newArray3D(
      appMemory,
      deleter,
      userdata,
      type,
      numItems1,
      numItems2,
      numItems3,
      byteStride1,
      byteStride2,
      byteStride3);
    result = wrapNewObject(result, ANARI_ARRAY3D);

    // find canonical strides
    uint64_t elementSize = anari_type_size(type);
    uint64_t sourceStride1 = byteStride1 > 0 ? byteStride1 : elementSize;
    uint64_t sourceStride2 = byteStride2 > 0 ? byteStride2 : sourceStride1*numItems1;
    uint64_t sourceStride3 = byteStride3 > 0 ? byteStride3 : sourceStride2*numItems2;

    ObjectData *obj = getObject(result);
    obj->externalArray = static_cast<char*>(appMemory);
    obj->arrayType = type;
    obj->arrayItems[0] = numItems1;
    obj->arrayItems[1] = numItems2;
    obj->arrayItems[2] = numItems3;
    obj->arrayStrides[0] = sourceStride1;
    obj->arrayStrides[1] = sourceStride2;
    obj->arrayStrides[2] = sourceStride3;

    SCOPE_RETURN(result)
}


void *DebugDevice::mapArray(ANARIArray array) {
  MAKE_SCOPE(anariMapArray, array)

  void *result = forward->mapArray(array);
  if(ObjectData *obj = getObject(array)) {
    obj->externalArray = static_cast<char*>(result);
  }

  SCOPE_RETURN(result)
}

void DebugDevice::unmapArray(ANARIArray array) {
  MAKE_SCOPE(anariUnmapArray, array)

  if(ObjectData *obj = getObject(array)) {

    if(wrapHandles && isObject(obj->arrayType)) {
      if(char *raw_objects = obj->externalArray) {
        for(uint64_t i = 0;i<obj->arrayItems[0];++i) {
          char *itemPtr = raw_objects + i*obj->arrayStrides[0];
          ANARIObject *wrapped = reinterpret_cast<ANARIObject*>(itemPtr);
          obj->ownedArray[i] = unwrapObject(*wrapped);
        }
      }
    }

    // capture changes
  }

  forward->unmapArray(array);
}

void DebugDevice::arrayRangeUpdated1D(
    ANARIArray1D array, uint64_t startIndex1, uint64_t elementCount1) {
  MAKE_SCOPE(anariArrayRangeUpdated1D, array,
    startIndex1, elementCount1)

  forward->arrayRangeUpdated1D(array, startIndex1, elementCount1);
}

void DebugDevice::arrayRangeUpdated2D(ANARIArray2D array,
    uint64_t startIndex1,
    uint64_t startIndex2,
    uint64_t elementCount1,
    uint64_t elementCount2) {
  MAKE_SCOPE(anariArrayRangeUpdated2D, array,
    startIndex1, startIndex2,
    elementCount1, elementCount2)

  forward->arrayRangeUpdated2D(array,
    startIndex1, startIndex2,
    elementCount1, elementCount2);
}

void DebugDevice::arrayRangeUpdated3D(ANARIArray3D array,
    uint64_t startIndex1,

    uint64_t startIndex2,
    uint64_t startIndex3,
    uint64_t elementCount1,
    uint64_t elementCount2,
    uint64_t elementCount3) {
  MAKE_SCOPE(anariArrayRangeUpdated3D, array,
    startIndex1, startIndex2, startIndex3,
    elementCount1, elementCount2, elementCount3)

  forward->arrayRangeUpdated3D(array,
    startIndex1, startIndex2, startIndex3,
    elementCount1, elementCount2, elementCount3);
}

// Renderable Objects ///////////////////////////////////////////////////////

ANARILight DebugDevice::newLight(const char *type) {
  MAKE_SCOPE(anariNewLight, type)

  ANARILight result = forward->newLight(type);
  result = wrapNewObject(result, ANARI_LIGHT);
  SCOPE_RETURN(result)
}

ANARICamera DebugDevice::newCamera(const char *type) {
  MAKE_SCOPE(anariNewCamera, type)

  ANARICamera result = forward->newCamera(type);
  result = wrapNewObject(result, ANARI_CAMERA);
  SCOPE_RETURN(result)
}

ANARIGeometry DebugDevice::newGeometry(const char *type) {
  MAKE_SCOPE(anariNewGeometry, type)

  ANARIGeometry result = forward->newGeometry(type);
  result = wrapNewObject(result, ANARI_GEOMETRY);
  SCOPE_RETURN(result)
}

ANARISpatialField DebugDevice::newSpatialField(const char *type) {
  MAKE_SCOPE(anariNewSpatialField, type)

  ANARISpatialField result = forward->newSpatialField(type);
  result = wrapNewObject(result, ANARI_SPATIAL_FIELD);
  SCOPE_RETURN(result)
}
ANARISurface DebugDevice::newSurface() {
  MAKE_SCOPE_NO_ARGS(anariNewSurface)

  ANARISurface result = forward->newSurface();
  result = wrapNewObject(result, ANARI_SURFACE);
  SCOPE_RETURN(result)
}

ANARIVolume DebugDevice::newVolume(const char *type) {
  MAKE_SCOPE(anariNewVolume, type)

  ANARIVolume result = forward->newVolume(type);
  result = wrapNewObject(result, ANARI_VOLUME);
  SCOPE_RETURN(result)
}

// Model Meta-Data //////////////////////////////////////////////////////////

ANARIMaterial DebugDevice::newMaterial(const char *material_type) {
  MAKE_SCOPE(anariNewMaterial, material_type)

  ANARIMaterial result = forward->newMaterial(material_type);
  result = wrapNewObject(result, ANARI_MATERIAL);
  SCOPE_RETURN(result)
}

ANARISampler DebugDevice::newSampler(const char *type) {
  MAKE_SCOPE(anariNewSampler, type)

  ANARISampler result = forward->newSampler(type);
  result = wrapNewObject(result, ANARI_SAMPLER);
  SCOPE_RETURN(result)
}

// Instancing ///////////////////////////////////////////////////////////////

ANARIGroup DebugDevice::newGroup() {
  MAKE_SCOPE_NO_ARGS(anariNewGroup)

  ANARIGroup result = forward->newGroup();
  result = wrapNewObject(result, ANARI_GROUP);
  SCOPE_RETURN(result)
}

ANARIInstance DebugDevice::newInstance() {
  MAKE_SCOPE_NO_ARGS(anariNewInstance)

  ANARIInstance result = forward->newInstance();
  result = wrapNewObject(result, ANARI_INSTANCE);
  SCOPE_RETURN(result)
}

// Top-level Worlds /////////////////////////////////////////////////////////

ANARIWorld DebugDevice::newWorld() {
  MAKE_SCOPE_NO_ARGS(anariNewWorld)

  ANARIWorld result = forward->newWorld();
  result = wrapNewObject(result, ANARI_WORLD);
  SCOPE_RETURN(result)
}

// Object + Parameter Lifetime Management ///////////////////////////////////

void DebugDevice::setParameter(ANARIObject object,
    const char *name,
    ANARIDataType type,
    const void *mem) {
  MAKE_SCOPE(anariSetParameter, object, name, type, mem)

  size_t logBegin = logPos();

  if(isObject(type)) {
    ANARIObject obj = unwrapObject(*static_cast<const ANARIObject*>(mem));
    forward->setParameter(unwrapObject(object), name, type, &obj);
  } else {
    forward->setParameter(unwrapObject(object), name, type, mem);
  }

  ObjectData *objdata = getObject(object);

  if(std::ostream *pout = getCaptureStream()) {
    std::ostream &out = *pout;
    int argnum = 0;
    if(!isObject(type)) {
      argnum = nextArgumentID++;
      anari_type_definition(out, type, mem, "arg", argnum, this);
    }

    out << scope.getName() << "(device, ";
    out << getObjectName(object);
    out << ", \"" << name << "\", " << anari_type_enumname(type) << ", ";

    if(!isObject(type)) {
      out << "arg" << argnum;
    } else { // this is an object
      out << '&' << getObjectName(*static_cast<const ANARIObject*>(mem));
    }
    out << ");\n";

    scope.captured = true;
  }

  if(isObject(type)) {
    if(ObjectData *data = getObject(*static_cast<const ANARIObject*>(mem))) {
      if(type != data->getType()) {
        reportStatus("object %p (%s) set as parameter on %p (%s) is of type %s but is set as %s.",
          *static_cast<const ANARIObject*>(mem), data->getName().c_str(),
          object, objdata?objdata->getName().c_str():"unknown",
          anari_type_name(data->getType()), anari_enum_name(type));
      }
      if(data->isDirty()) {
        reportStatus("object %p (%s) set as parameter on %p (%s) has uncommited changes",
          *static_cast<const ANARIObject*>(mem), data->getName().c_str(),
          object, objdata?objdata->getName().c_str():"unknown");
      }
      if(data->getRefcount() <= 0) {
        reportStatus("object %p (%s) set as parameter on %p (%s) has been previously released (refcount = %d)",
          *static_cast<const ANARIObject*>(mem), data->getName().c_str(),
          object, objdata?objdata->getName().c_str():"unknown", data->getRefcount());
      }
    }
  }

  size_t logEnd = logPos();

  if(objdata) {
    //objdata->dirty = true;
    objdata->setParam(name, type, mem, logBegin, logEnd);
  }
}

void DebugDevice::unsetParameter(ANARIObject object, const char *name) {
  MAKE_SCOPE(anariUnsetParameter, object, name)

  forward->unsetParameter(unwrapObject(object), name);
}

void DebugDevice::commit(ANARIObject object) {
  MAKE_SCOPE(anariCommit, object)
  size_t logBegin = logPos();

  forward->commit(unwrapObject(object));

  if(ObjectData *data = getObject(object)) {
    if(!data->isDirty()) {
      reportStatus("redundant commit on object %p (%s)", object, data->getName().c_str());
    }

    size_t logEnd = logPos();
    data->commit(logBegin, logEnd);
  }
}

void DebugDevice::release(ANARIObject object) {
  MAKE_SCOPE(anariRelease, object)

  if(ObjectData *data = getObject(object)) {

    if(object != nullptr) { // don't release the null handle
      //data->public_refcount -= 1;
      data->release();
    }
    if(data->getRefcount() < 0) {
      reportStatus("object %p (%s) refcount %d", object, data->getName().c_str(), data->getRefcount());
    }
  }

  forward->release(unwrapObject(object));
}
void DebugDevice::retain(ANARIObject object) {
  MAKE_SCOPE(anariRetain, object)

  if(ObjectData *data = getObject(object)) {
    if(object != nullptr) { // don't retain the null handle
      //data->public_refcount += 1;
      data->retain();
    }
  }

  forward->retain(unwrapObject(object));
}

// Object Query Interface ///////////////////////////////////////////////////

int DebugDevice::getProperty(ANARIObject object,
  const char *name,
  ANARIDataType type,
  void *mem,
  uint64_t size,
  ANARIWaitMask mask)
{
  // queries are not traced currently since they don't manipulate the device
  // state (presumably) and are effectively no-ops for the trace
  if(name == std::string("debug.name")) {
    // getObjectName(object);
    // how does returning strings work here? what is their lifetime?
    return 0;
  } else if ((void*)object == (void *)forward.get()) {
    return forward->getProperty((ANARIDevice)forward.get(), name, type, mem, size, mask);
  } else {
    return forward->getProperty(unwrapObject(object), name, type, mem, size, mask);
  }
}

// FrameBuffer Manipulation /////////////////////////////////////////////////

ANARIFrame DebugDevice::frameCreate() {
  MAKE_SCOPE_NO_ARGS(anariNewFrame)

  ANARIFrame result = forward->frameCreate();
  result = wrapNewObject(result, ANARI_FRAME);
  SCOPE_RETURN(result)
}

const void *DebugDevice::frameBufferMap(
    ANARIFrame fb, const char *channel) {
  MAKE_SCOPE(anariMapFrame, fb, channel)

  const void *ptr = forward->frameBufferMap(unwrapObject(fb), channel);
  newPointer(ptr);

  if(std::ostream *pout = getCaptureStream()) {
    std::ostream &out = *pout;

    const std::string ptrname = getPointerName(ptr);
    out << "const void* " << ptrname << " = anariMapFrame(device, ";
    out << getObjectName(fb);
    out << ", \"" << channel << "\");\n";

    if(ObjectData *data = getObject(fb)) {
      int size[2] = {-1, -1};
      ANARIDataType format;
      data->getParam("size", ANARI_UINT32_VEC2, size);
      data->getParam("color", ANARI_DATA_TYPE, &format);
      out << "writeImage(\"" << getObjectName(fb) << "\", " << ptrname << ", " << size[0] << ", " << size[1] << ", " << format << ", \"" << channel << "\");\n";
    }

    scope.captured = true;
  }

  SCOPE_RETURN(ptr);
}

void DebugDevice::frameBufferUnmap(ANARIFrame fb, const char *channel) {
  MAKE_SCOPE(anariUnmapFrame, fb, channel)

  forward->frameBufferUnmap(unwrapObject(fb), channel);
}

// Frame Rendering //////////////////////////////////////////////////////////

ANARIRenderer DebugDevice::newRenderer(const char *type) {
  MAKE_SCOPE(anariNewRenderer, type)

  ANARIRenderer result = forward->newRenderer(type);
  result = wrapNewObject(result, ANARI_RENDERER);
  SCOPE_RETURN(result)
}

ANARIObject DebugDevice::newObject(const char *type, const char *subtype) {
  MAKE_SCOPE(anariNewObject, type, subtype)

  ANARIObject result = forward->newObject(type, subtype);
  result = wrapNewObject(result, ANARI_OBJECT);
  SCOPE_RETURN(result)
}

void DebugDevice::renderFrame(ANARIFrame frame) {
  MAKE_SCOPE(anariRenderFrame, frame)

  forward->renderFrame(unwrapObject(frame));
}

int DebugDevice::frameReady(ANARIFrame frame, ANARIWaitMask mask) {
  MAKE_SCOPE(anariFrameReady, frame, mask)

  SCOPE_RETURN(forward->frameReady(unwrapObject(frame), mask))
}
void DebugDevice::discardFrame(ANARIFrame frame) {
  MAKE_SCOPE(anariDiscardFrame, frame)

  forward->discardFrame(unwrapObject(frame));
}

DebugDevice::~DebugDevice() {
  for(auto i : objectMap) {
    ObjectData &data = i.second;
    if(data.getRefcount()>0 && i.first != nullptr && i.first != (ANARIObject)forward.get()) {
      reportStatus("object %p (%s) leaked", i.first, i.second.getName().c_str());
#ifdef PRINT_BACKTRACES
      if(print_backtraces) {
        char **symbols = backtrace_symbols(i.second.trace_buffer, i.second.trace_length);
        reportStatus("allocated at:");

        // start at 3 to skip the debug layer internals
        for(int j = 3;j<i.second.trace_length;++j) {
          reportStatus("   %s ", symbols[j]);
        }
        free(symbols);
      }
#endif
    }
  }
}

static void anariInsertStatusMessageImpl(ANARIDevice device, const char *message) {
  anari::Device *d = reinterpret_cast<anari::Device*>(device);
  if(DebugDevice *dd = dynamic_cast<DebugDevice*>(d)) {
    dd->insertStatusMessage(message);
  }
}

static void anariNameObjectImpl(ANARIDevice device, ANARIObject object, const char *name) {
  anari::Device *d = reinterpret_cast<anari::Device*>(device);
  if(DebugDevice *dd = dynamic_cast<DebugDevice*>(d)) {
    dd->nameObject(object, name);
  }
}

void (*DebugDevice::getProcAddress(const char *name))(void) {
  if(name == std::string("anariInsertStatusMessage")) {
    return (void (*)())anariInsertStatusMessageImpl;
  } else if(name == std::string("anariNameObject")) {
    return (void (*)())anariNameObjectImpl;
  } else {
    return forward->getProcAddress(name);
  }
}

ANARI_DEFINE_LIBRARY_INIT(debug)
{
  printf("...loaded debug library!\n");
  anari::Device::registerLayer<DebugDevice>();
}
