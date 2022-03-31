// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ExampleDevice.h"

#include "anari/detail/Library.h"
#include "include/anari/devices/example.h"

#include "Array.h"
#include "frame/Frame.h"
#include "material/Material.h"
#include "renderer/Renderer.h"
#include "scene/World.h"

// debug interface
#include "DebugObject.h"

// std
#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <limits>
#include <thread>

namespace anari {
namespace example_device {

///////////////////////////////////////////////////////////////////////////////
// Helper functions ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template <typename T>
inline void writeToVoidP(void *_p, T v)
{
  T *p = (T *)_p;
  *p = v;
}

template <typename T, typename... Args>
inline T *createRegisteredObject(Args &&...args)
{
  return new T(std::forward<Args>(args)...);
}

template <typename HANDLE_T, typename OBJECT_T>
inline HANDLE_T getHandleForAPI(OBJECT_T *object)
{
  return (HANDLE_T)object;
}

template <typename OBJECT_T, typename HANDLE_T, typename... Args>
inline HANDLE_T createObjectForAPI(Args &&...args)
{
  auto o = createRegisteredObject<OBJECT_T>(std::forward<Args>(args)...);
  o->setObjectType(ANARITypeFor<HANDLE_T>::value);
  return getHandleForAPI<HANDLE_T>(o);
}

template <typename HANDLE_T>
inline HANDLE_T createPlaceholderObject()
{
  return createObjectForAPI<Object, HANDLE_T>();
}

template <typename OBJECT_T = Object, typename HANDLE_T = ANARIObject>
inline OBJECT_T &referenceFromHandle(HANDLE_T handle)
{
  return *((OBJECT_T *)handle);
}

using SetParamFcn = void(ANARIObject, const char *, const void *);

template <typename T>
static void setParamOnObject(ANARIObject obj, const char *p, const T &v)
{
  referenceFromHandle(obj).setParam(p, v);
}

static void removeParamOnObject(ANARIObject obj, const char *p)
{
  referenceFromHandle(obj).removeParam(p);
}

#define declare_param_setter(TYPE)                                             \
  {                                                                            \
    ANARITypeFor<TYPE>::value,                                                 \
        [](ANARIObject o, const char *p, const void *v) {                      \
          setParamOnObject(o, p, *(TYPE *)v);                                  \
        }                                                                      \
  }

#define declare_param_setter_object(TYPE)                                      \
  {                                                                            \
    ANARITypeFor<TYPE>::value,                                                 \
        [](ANARIObject o, const char *p, const void *v) {                      \
          using OBJECT_T = typename std::remove_pointer<TYPE>::type;           \
          auto ptr = *((TYPE *)v);                                             \
          if (ptr)                                                             \
            setParamOnObject(o, p, IntrusivePtr<OBJECT_T>(ptr));               \
          else                                                                 \
            removeParamOnObject(o, p);                                         \
        }                                                                      \
  }

#define declare_param_setter_string(TYPE)                                      \
  {                                                                            \
    ANARITypeFor<TYPE>::value,                                                 \
        [](ANARIObject o, const char *p, const void *v) {                      \
          const char *str = (const char *)v;                                   \
          setParamOnObject(o, p, std::string(str));                            \
        }                                                                      \
  }

#define declare_param_setter_void_ptr(TYPE)                                    \
  {                                                                            \
    ANARITypeFor<TYPE>::value,                                                 \
        [](ANARIObject o, const char *p, const void *v) {                      \
          setParamOnObject(o, p, const_cast<void *>(v));                       \
        }                                                                      \
  }

static std::map<int, SetParamFcn *> setParamFcns = {
    declare_param_setter(ANARIDataType),
    declare_param_setter(Device *),
    declare_param_setter_void_ptr(void *),
    declare_param_setter(ANARIFrameCompletionCallback),
    declare_param_setter(bool),
    declare_param_setter_object(Object *),
    declare_param_setter_object(Camera *),
    declare_param_setter_object(Array *),
    declare_param_setter_object(Array1D *),
    declare_param_setter_object(Array2D *),
    declare_param_setter_object(Array3D *),
    declare_param_setter_object(Frame *),
    declare_param_setter_object(Geometry *),
    declare_param_setter_object(Group *),
    declare_param_setter_object(Instance *),
    // declare_param_setter_object(Light *),
    declare_param_setter_object(Material *),
    declare_param_setter_object(Renderer *),
    // declare_param_setter_object(Sampler *),
    declare_param_setter_object(Surface *),
    declare_param_setter_object(SpatialField *),
    declare_param_setter_object(Volume *),
    declare_param_setter_object(World *),
    declare_param_setter_string(const char *),
    declare_param_setter(char),
    declare_param_setter(unsigned char),
    declare_param_setter(short),
    declare_param_setter(int),
    declare_param_setter(unsigned int),
    declare_param_setter(long),
    declare_param_setter(unsigned long),
    declare_param_setter(long long),
    declare_param_setter(unsigned long long),
    declare_param_setter(float),
    declare_param_setter(double),
    declare_param_setter(ivec2),
    declare_param_setter(ivec3),
    declare_param_setter(ivec4),
    declare_param_setter(uvec2),
    declare_param_setter(uvec3),
    declare_param_setter(uvec4),
    declare_param_setter(vec2),
    declare_param_setter(vec3),
    declare_param_setter(vec4),
    declare_param_setter(mat2),
    declare_param_setter(mat3),
    declare_param_setter(mat3x2),
    declare_param_setter(mat4x3),
    declare_param_setter(mat4)};

#undef declare_param_setter
#undef declare_param_setter_object
#undef declare_param_setter_string
#undef declare_param_setter_void_ptr

///////////////////////////////////////////////////////////////////////////////
// ExampleDevice definitions //////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int ExampleDevice::deviceImplements(const char *_extension)
{
  std::string extension = _extension;
  if (extension == ANARI_KHR_FRAME_COMPLETION_CALLBACK)
    return 1;
  return 0;
}

void ExampleDevice::deviceSetParameter(
    const char *_id, ANARIDataType type, const void *mem)
{
  std::string id = _id;
  if (id == "numThreads" && type == ANARI_INT32)
    setParam(id, *static_cast<const int *>(mem));
}

void ExampleDevice::deviceUnsetParameter(const char *id)
{
  removeParam(id);
}

void ExampleDevice::deviceCommit()
{
  m_numThreads =
      getParam<int>("numThreads", std::thread::hardware_concurrency() - 1);
  m_numThreads = std::max(m_numThreads, 1);
}

void ExampleDevice::deviceRetain()
{
  this->refInc();
}

void ExampleDevice::deviceRelease()
{
  this->refDec();
}

// Data Arrays ////////////////////////////////////////////////////////////////

ANARIArray1D ExampleDevice::newArray1D(void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userData,
    ANARIDataType type,
    uint64_t numItems,
    uint64_t byteStride)
{
  if (isObject(type)) {
    return createObjectForAPI<ObjectArray, ANARIArray1D>(
        appMemory, deleter, userData, type, numItems, byteStride);
  } else {
    return createObjectForAPI<Array1D, ANARIArray1D>(
        appMemory, deleter, userData, type, numItems, byteStride);
  }
}

ANARIArray2D ExampleDevice::newArray2D(void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1,
    uint64_t byteStride2)
{
  return createObjectForAPI<Array2D, ANARIArray2D>(appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2,
      byteStride1,
      byteStride2);
}

ANARIArray3D ExampleDevice::newArray3D(void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
{
  return createObjectForAPI<Array3D, ANARIArray3D>(appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2,
      numItems3,
      byteStride1,
      byteStride2,
      byteStride3);
}

void *ExampleDevice::mapArray(ANARIArray a)
{
  return referenceFromHandle<Array>(a).map();
}

void ExampleDevice::unmapArray(ANARIArray a)
{
  referenceFromHandle<Array>(a).unmap();
}

// Renderable Objects /////////////////////////////////////////////////////////

ANARILight ExampleDevice::newLight(const char *type)
{
  return createPlaceholderObject<ANARILight>();
}

ANARICamera ExampleDevice::newCamera(const char *type)
{
  return (ANARICamera)Camera::createInstance(type);
}

ANARIGeometry ExampleDevice::newGeometry(const char *type)
{
  return (ANARIGeometry)Geometry::createInstance(type);
}

ANARISpatialField ExampleDevice::newSpatialField(const char *type)
{
  return (ANARISpatialField)SpatialField::createInstance(type);
}

ANARISurface ExampleDevice::newSurface()
{
  return createObjectForAPI<Surface, ANARISurface>();
}

ANARIVolume ExampleDevice::newVolume(const char *_type)
{
  std::string type(_type);
  if (type == "density" || type == "scivis")
    return createObjectForAPI<Volume, ANARIVolume>();
  throw std::runtime_error("could not create volume");
}

// Model Meta-Data ////////////////////////////////////////////////////////////

ANARIMaterial ExampleDevice::newMaterial(const char *type)
{
  return (ANARIMaterial)Material::createInstance(type);
}

ANARISampler ExampleDevice::newSampler(const char *type)
{
  return createPlaceholderObject<ANARISampler>();
}

// Instancing /////////////////////////////////////////////////////////////////

ANARIGroup ExampleDevice::newGroup()
{
  return createObjectForAPI<Group, ANARIGroup>();
}

ANARIInstance ExampleDevice::newInstance()
{
  return createObjectForAPI<Instance, ANARIInstance>();
}

// Top-level Worlds ///////////////////////////////////////////////////////////

ANARIWorld ExampleDevice::newWorld()
{
  return createObjectForAPI<World, ANARIWorld>();
}

anari::debug_device::ObjectFactory *getDebugFactory();

int ExampleDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask)
{
  if (mask == ANARI_WAIT)
    flushCommitBuffer();

  if ((void *)object == (void *)this) {
    std::string_view prop = name;
    if (prop == "version" && type == ANARI_INT32) {
      writeToVoidP(mem, DEVICE_VERSION);
      return 1;
    } else if (prop == "debugObjects" && type == ANARI_FUNCTION_POINTER) {
      writeToVoidP(mem, getDebugFactory);
      return 1;
    }
  } else
    return referenceFromHandle(object).getProperty(name, type, mem, mask);

  return 0;
}

// Object + Parameter Lifetime Management /////////////////////////////////////

void ExampleDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  if (type == ANARI_UNKNOWN)
    throw std::runtime_error("cannot set ANARI_UNKNOWN parameter type");

  auto *fcn = setParamFcns[type];

  if (fcn)
    fcn(object, name, mem);
  else {
    fprintf(stderr,
        "warning - no parameter setter for type '%s'"
        ", '%s' parameter will be ignored\n",
        toString(type),
        name);
  }
}

void ExampleDevice::unsetParameter(ANARIObject o, const char *name)
{
  referenceFromHandle(o).removeParam(name);
}

void ExampleDevice::commit(ANARIObject h)
{
  auto &o = referenceFromHandle(h);
  o.refInc(RefType::INTERNAL);
  if (o.commitPriority() != COMMIT_PRIORITY_DEFAULT)
    m_needToSortCommits = true;
  m_objectsToCommit.push_back(&o);
}

void ExampleDevice::release(ANARIObject o)
{
  if (!o)
    return;

  auto &obj = referenceFromHandle(o);

  bool privatizeArray = isArray(obj.type())
      && obj.useCount(RefType::INTERNAL) > 0
      && obj.useCount(RefType::PUBLIC) == 1;

  referenceFromHandle(o).refDec(RefType::PUBLIC);

  if (privatizeArray)
    ((Array *)o)->privatize();
}

void ExampleDevice::retain(ANARIObject o)
{
  referenceFromHandle(o).refInc(RefType::PUBLIC);
}

// Frame Manipulation /////////////////////////////////////////////////////////

ANARIFrame ExampleDevice::newFrame()
{
  return createObjectForAPI<Frame, ANARIFrame>();
}

const void *ExampleDevice::frameBufferMap(ANARIFrame _fb, const char *channel)
{
  auto &fb = referenceFromHandle<Frame>(_fb);

  if (channel == std::string("color"))
    return fb.mapColorBuffer();
  else if (channel == std::string("depth"))
    return fb.mapDepthBuffer();
  else
    return nullptr;
}

void ExampleDevice::frameBufferUnmap(ANARIFrame, const char *)
{
  // no-op
}

// Frame Rendering ////////////////////////////////////////////////////////////

ANARIRenderer ExampleDevice::newRenderer(const char *type)
{
  return (ANARIRenderer)Renderer::createInstance(type);
}

void ExampleDevice::renderFrame(ANARIFrame frame)
{
  flushCommitBuffer();

  auto *f = &referenceFromHandle<Frame>(frame);

  auto device_handle = this_device();

  f->renewFuture();
  auto future = f->future();

  std::thread([=]() {
    auto start = std::chrono::steady_clock::now();
    f->renderFrame(m_numThreads);
    auto end = std::chrono::steady_clock::now();

    f->invokeContinuation(device_handle);
    f->setDuration(std::chrono::duration<float>(end - start).count());

    future->markComplete();
  }).detach();
}

int ExampleDevice::frameReady(ANARIFrame f, ANARIWaitMask m)
{
  auto &frame = referenceFromHandle<Frame>(f);
  if (!frame.futureIsValid())
    return 0;
  else if (m == ANARI_NO_WAIT)
    return frame.future()->isReady();
  else {
    frame.future()->wait();
    return 1;
  }
}

void ExampleDevice::discardFrame(ANARIFrame)
{
  // TODO
}

// Other ExampleDevice definitions ////////////////////////////////////////////

ExampleDevice::ExampleDevice()
{
  deviceCommit();
}

void ExampleDevice::flushCommitBuffer()
{
  if (m_needToSortCommits) {
    std::sort(m_objectsToCommit.begin(),
        m_objectsToCommit.end(),
        [](Object *o1, Object *o2) {
          return o1->commitPriority() < o2->commitPriority();
        });
  }

  m_needToSortCommits = false;

  for (auto o : m_objectsToCommit) {
    o->commit();
    o->markUpdated();
    o->refDec(RefType::INTERNAL);
  }

  m_objectsToCommit.clear();
}

const char **query_object_types(ANARIDataType type);
const ANARIParameter *query_params(ANARIDataType type, const char *subtype);

} // namespace example_device
} // namespace anari

static char deviceName[] = "example";

extern "C" EXAMPLE_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_NEW_DEVICE(
    example, subtype)
{
  if (subtype == std::string("default") || subtype == std::string("example"))
    return (ANARIDevice) new anari::example_device::ExampleDevice();
  return nullptr;
}

extern "C" EXAMPLE_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_INIT(example)
{
  printf("...loaded example library!\n");
}

extern "C" EXAMPLE_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_DEVICE_SUBTYPES(
    example, libdata)
{
  static const char *devices[] = {deviceName, nullptr};
  return devices;
}

extern "C" EXAMPLE_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_SUBTYPES(
    example, libdata, deviceSubtype, objectType)
{
  /*
  if (objectType == ANARI_RENDERER) {
    static std::vector<const char *> renderers;
    renderers.clear();
    anari::example_device::Renderer::init();
    for (auto &r : *anari::example_device::Renderer::g_renderers)
      renderers.push_back(r.first.c_str());
    renderers.push_back(nullptr);
    return renderers.data();
  }
  return nullptr;
  */
  return anari::example_device::query_object_types(objectType);
}

extern "C" EXAMPLE_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_PARAMETERS(
    example, libdata, deviceSubtype, objectSubtype, objectType)
{
  /*
  if (objectType == ANARI_RENDERER)
    return anari::example_device::Renderer::g_parameters;
  return nullptr;
  */
  return anari::example_device::query_params(objectType, objectSubtype);
}

extern "C" EXAMPLE_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_PARAMETER_PROPERTY(
    example,
    libdata,
    deviceSubtype,
    objectSubtype,
    objectType,
    parameterName,
    parameterType,
    propertyName,
    propertyType)
{
  if (objectType == ANARI_RENDERER) {
    int i = 0;
    while (anari::example_device::Renderer::g_parameters[i].name != nullptr) {
      if (std::string(anari::example_device::Renderer::g_parameters[i].name)
              == std::string(parameterName)
          && (anari::example_device::Renderer::g_parameters[i].type
              == parameterType))
        break;

      i++;
    }
    if (anari::example_device::Renderer::g_parameters[i].name == nullptr)
      return nullptr;
    if (propertyType == ANARI_STRING
        && std::string(propertyName) == std::string("description"))
      return anari::example_device::Renderer::g_parameterinfos[i].desc;
    if (propertyType == ANARI_BOOL
        && std::string(propertyName) == std::string("required"))
      return &anari::example_device::Renderer::g_parameterinfos[i].req;
    if (propertyType == parameterType
        && std::string(propertyName) == std::string("default"))
      return &anari::example_device::Renderer::g_parameterinfos[i].def;
  }
  return nullptr;
}

extern "C" EXAMPLE_DEVICE_INTERFACE ANARIDevice anariNewExampleDevice()
{
  return (ANARIDevice) new anari::example_device::ExampleDevice();
}
