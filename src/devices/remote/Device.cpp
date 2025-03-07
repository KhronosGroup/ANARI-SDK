//  Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Device.h"
#include <anari/anari_cpp.hpp>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include "ArrayInfo.h"
#include "Compression.h"
#include "Frame.h"
#include "Logging.h"
#include "ObjectDesc.h"
#include "async/connection.h"
#include "async/connection_manager.h"
#include "async/work_queue.h"
#include "common.h"

#ifndef _WIN32
#include <sys/time.h>
#else
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

using namespace std::placeholders;
using namespace helium;

inline double getCurrentTime()
{
#ifndef _WIN32
  struct timeval tp;
  gettimeofday(&tp, nullptr);
  return double(tp.tv_sec) + double(tp.tv_usec) / 1E6;
#else
  return 0.0;
#endif
}

namespace remote {

//--- Data Arrays -------------------------------------

void *Device::mapParameterArray1D(ANARIObject o,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t *elementStride)
{
  auto array = newArray1D(nullptr, nullptr, nullptr, dataType, numElements1);
  ParameterArray pa{o, name};
  parameterArrays[pa] = array;
  setParameter(o, name, ANARI_ARRAY1D, &array);
  *elementStride = anari::sizeOf(dataType);
  return mapArray(array);
}

void *Device::mapParameterArray2D(ANARIObject o,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t *elementStride)
{
  auto array = newArray2D(
      nullptr, nullptr, nullptr, dataType, numElements1, numElements2);
  ParameterArray pa{o, name};
  parameterArrays[pa] = array;
  setParameter(o, name, ANARI_ARRAY2D, &array);
  *elementStride = anari::sizeOf(dataType);
  return mapArray(array);
}

void *Device::mapParameterArray3D(ANARIObject o,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3,
    uint64_t *elementStride)
{
  auto array = newArray3D(nullptr,
      nullptr,
      nullptr,
      dataType,
      numElements1,
      numElements2,
      numElements3);
  ParameterArray pa{o, name};
  parameterArrays[pa] = array;
  setParameter(o, name, ANARI_ARRAY3D, &array);
  *elementStride = anari::sizeOf(dataType);
  return mapArray(array);
}

void Device::unmapParameterArray(ANARIObject o, const char *name)
{
  ParameterArray pa{o, name};
  auto it = parameterArrays.find(pa);
  if (it != parameterArrays.end()) {
    ANARIArray array = it->second;
    unmapArray(array);
    parameterArrays.erase(pa);
  }
}

ANARIArray1D Device::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userPtr,
    const ANARIDataType elementType,
    uint64_t numItems1)
{
  return (ANARIArray1D)registerNewArray(
      ANARI_ARRAY1D, appMemory, deleter, userPtr, elementType, numItems1, 0, 0);
}

ANARIArray2D Device::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userPtr,
    ANARIDataType elementType,
    uint64_t numItems1,
    uint64_t numItems2)
{
  return (ANARIArray2D)registerNewArray(ANARI_ARRAY2D,
      appMemory,
      deleter,
      userPtr,
      elementType,
      numItems1,
      numItems2,
      0);
}

ANARIArray3D Device::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userPtr,
    ANARIDataType elementType,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  return (ANARIArray3D)registerNewArray(ANARI_ARRAY3D,
      appMemory,
      deleter,
      userPtr,
      elementType,
      numItems1,
      numItems2,
      numItems3);
}

void *Device::mapArray(ANARIArray array)
{
  auto buf = std::make_shared<Buffer>();
  buf->write(ObjectDesc(remoteDevice, array));
  write(MessageType::MapArray, buf);

  std::unique_lock l(sync[SyncPoints::MapArray].mtx);
  ArrayData &data = arrays[array];
  sync[SyncPoints::MapArray].cv.wait(
      l, [&]() { return (std::int64_t)data.value.size() == data.bytesExpected; });
  l.unlock();

  LOG(logging::Level::Info) << "Array mapped: " << array;

  return data.value.data();
}

void Device::unmapArray(ANARIArray array)
{
  auto buf = std::make_shared<Buffer>();
  buf->write(ObjectDesc(remoteDevice, array));
  uint64_t numBytes = arrays[array].value.size();
  buf->write(arrays[array].value.data(), numBytes);
  write(MessageType::UnmapArray, buf);

  std::unique_lock l(sync[SyncPoints::MapArray].mtx);
  ArrayData &data = arrays[array];
  sync[SyncPoints::MapArray].cv.wait(l, [&]() { return data.value.empty(); });
  l.unlock();

  arrays.erase(array);

  LOG(logging::Level::Info) << "Array unmapped: " << array;
}

//--- Renderable Objects ------------------------------

ANARILight Device::newLight(const char *type)
{
  return (ANARILight)registerNewObject(ANARI_LIGHT, type);
}

ANARICamera Device::newCamera(const char *type)
{
  return (ANARICamera)registerNewObject(ANARI_CAMERA, type);
}

ANARIGeometry Device::newGeometry(const char *type)
{
  return (ANARIGeometry)registerNewObject(ANARI_GEOMETRY, type);
}

ANARISpatialField Device::newSpatialField(const char *type)
{
  return (ANARISpatialField)registerNewObject(ANARI_SPATIAL_FIELD, type);
}

ANARISurface Device::newSurface()
{
  return (ANARISurface)registerNewObject(ANARI_SURFACE);
}

ANARIVolume Device::newVolume(const char *type)
{
  return (ANARIVolume)registerNewObject(ANARI_VOLUME, type);
}

//--- Surface Meta-Data -------------------------------

ANARIMaterial Device::newMaterial(const char *material_type)
{
  return (ANARIMaterial)registerNewObject(ANARI_MATERIAL, material_type);
}

ANARISampler Device::newSampler(const char *type)
{
  return (ANARISampler)registerNewObject(ANARI_SAMPLER, type);
}

//--- Instancing --------------------------------------

ANARIGroup Device::newGroup()
{
  return (ANARIGroup)registerNewObject(ANARI_GROUP);
}

ANARIInstance Device::newInstance(const char *type)
{
  return (ANARIInstance)registerNewObject(ANARI_INSTANCE, type);
}

//--- Top-level Worlds --------------------------------

ANARIWorld Device::newWorld()
{
  return (ANARIWorld)registerNewObject(ANARI_WORLD);
}

//--- Object + Parameter Lifetime Management ----------

void Device::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  if (!object) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return;
  }

  // Device parameters
  if (object == (ANARIObject)this) {
    if (std::string(name) == "server.hostname") {
      if (remoteDevice != nullptr) {
        LOG(logging::Level::Error)
            << "server.hostname must be set after device creation";
        return;
      }
      server.hostname = std::string((const char *)mem);
    } else if (std::string(name) == "server.port") {
      if (remoteDevice != nullptr) {
        LOG(logging::Level::Error)
            << "server.port must be set after device creation";
        return;
      }
      server.port = *(unsigned short *)mem;
    }
    // device parameter, don't write to socket!
    return;
  }

  auto buf = std::make_shared<Buffer>();
  buf->write(makeObjectDesc(object));
  buf->write(std::string(name));
  buf->write(type);

  if (anari::isObject(type)) {
    buf->write((const char *)mem, sizeof(ANARIObject));
  } else if (type == ANARI_STRING) {
    buf->write(std::string((const char *)mem));
  } else {
    buf->write((const char *)mem, anari::sizeOf(type));
  }

  write(MessageType::SetParam, buf);

  LOG(logging::Level::Info)
      << "Parameter " << name << " set on object " << object;
}

void Device::unsetParameter(ANARIObject object, const char *name)
{
  if (!object) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return;
  }

  auto buf = std::make_shared<Buffer>();
  buf->write(makeObjectDesc(object));
  buf->write(std::string(name));
  write(MessageType::UnsetParam, buf);

  LOG(logging::Level::Info)
      << "Parameter " << name << " unset on object " << object;
}

void Device::unsetAllParameters(ANARIObject object)
{
  if (!object) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return;
  }

  auto buf = std::make_shared<Buffer>();
  buf->write(makeObjectDesc(object));
  write(MessageType::UnsetAllParams, buf);

  LOG(logging::Level::Info)
      << "All parameters unset on object unset on object " << object;
}

void Device::commitParameters(ANARIObject object)
{
  if (!object) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return;
  }

  auto buf = std::make_shared<Buffer>();
  buf->write(makeObjectDesc(object));
  write(MessageType::CommitParams, buf);

  LOG(logging::Level::Info) << "Parameters committed on object " << object;
}

void Device::release(ANARIObject object)
{
  if (!object) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return;
  }

  // If this is  a frame, delete it from map
  if (frames.find(object) != frames.end())
    frames.erase(object);

  auto buf = std::make_shared<Buffer>();
  buf->write(makeObjectDesc(object));
  write(MessageType::Release, buf);

  LOG(logging::Level::Info) << "Object released: " << object;
}

void Device::retain(ANARIObject object)
{
  if (!object) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return;
  }

  auto buf = std::make_shared<Buffer>();
  buf->write(makeObjectDesc(object));
  write(MessageType::Retain, buf);

  LOG(logging::Level::Info) << "Object retained: " << object;
}

//--- Object Query Interface --------------------------

int Device::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask)
{
  if (!object) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return 0;
  }

  auto buf = std::make_shared<Buffer>();
  buf->write(makeObjectDesc(object));
  buf->write(std::string(name));
  buf->write(type);
  buf->write(size);
  buf->write(mask);
  write(MessageType::GetProperty, buf);

  std::unique_lock l(sync[SyncPoints::Properties].mtx);
  std::vector<Property>::iterator it;
  sync[SyncPoints::Properties].cv.wait(l, [this, &it, name]() {
    it = std::find_if(
        properties.begin(), properties.end(), [name](const Property &prop) {
          return prop.name == std::string(name);
        });
    return it != properties.end();
  });

  if (type == ANARI_STRING_LIST) {
    writeToVoidP(mem, it->value.asStringList.data());
  } else if (type == ANARI_DATA_TYPE_LIST) {
    throw std::runtime_error(
        "getProperty with ANARI_DATA_TYPE_LIST not implemented yet!");
  } else { // POD
    memcpy(mem, it->value.asAny.data(), size);
  }
  int result = it->result;
  l.unlock();

  return result;
}

const char **Device::getObjectSubtypes(ANARIDataType objectType)
{
  auto it = std::find_if(objectSubtypes.begin(),
      objectSubtypes.end(),
      [objectType](
          const ObjectSubtypes &os) { return os.objectType == objectType; });
  if (it != objectSubtypes.end()) {
    return (const char **)it->value.data();
  }

  auto buf = std::make_shared<Buffer>();
  buf->write(ObjectDesc(remoteDevice, remoteDevice));
  buf->write(objectType);
  write(MessageType::GetObjectSubtypes, buf);

  std::unique_lock l(sync[SyncPoints::ObjectSubtypes].mtx);
  sync[SyncPoints::ObjectSubtypes].cv.wait(l, [this, &it, objectType]() {
    it = std::find_if(objectSubtypes.begin(),
        objectSubtypes.end(),
        [&it, objectType](
            const ObjectSubtypes &os) { return os.objectType == objectType; });
    return it != objectSubtypes.end();
  });

  return (const char **)it->value.data();
}

const void *Device::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  const std::string subtype = objectSubtype ? objectSubtype : "";

  auto it = std::find_if(objectInfos.begin(),
      objectInfos.end(),
      [objectType, subtype, infoName, infoType](
          const ObjectInfo::Ptr &oi) {
        return oi->objectType == objectType
            && oi->objectSubtype == subtype
            && oi->info.name == std::string(infoName)
            && oi->info.type == infoType;
      });
  if (it != objectInfos.end()) {
    return (*it)->info.data();
  }

  auto buf = std::make_shared<Buffer>();
  buf->write(ObjectDesc(remoteDevice, remoteDevice));
  buf->write(objectType);
  buf->write(subtype);
  buf->write(std::string(infoName));
  buf->write(infoType);
  write(MessageType::GetObjectInfo, buf);

  std::unique_lock l(sync[SyncPoints::ObjectInfo].mtx);
  sync[SyncPoints::ObjectInfo].cv.wait(
      l, [this, &it, objectType, subtype, infoName, infoType]() {
        it = std::find_if(objectInfos.begin(),
            objectInfos.end(),
            [&it, objectType, subtype, infoName, infoType](
                const ObjectInfo::Ptr &oi) {
              return oi->objectType == objectType
                  //&& oi->objectSubtype == subtype
                  && oi->info.name == std::string(infoName)
                  && oi->info.type == infoType;
            });
        return it != objectInfos.end();
      });

  return (*it)->info.data();
}

const void *Device::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  const std::string subtype = objectSubtype ? objectSubtype : "";

  auto it = std::find_if(parameterInfos.begin(),
      parameterInfos.end(),
      [objectType,
          subtype,
          parameterName,
          parameterType,
          infoName,
          infoType](const ParameterInfo::Ptr &pi) {
        return pi->objectType == objectType
            && pi->objectSubtype == subtype
            && pi->parameterName == std::string(parameterName)
            && pi->parameterType == parameterType
            && pi->info.name == std::string(infoName)
            && pi->info.type == infoType;
      });
  if (it != parameterInfos.end()) {
    return (*it)->info.data();
  }

  auto buf = std::make_shared<Buffer>();
  buf->write(ObjectDesc(remoteDevice, remoteDevice));
  buf->write(objectType);
  buf->write(subtype);
  buf->write(std::string(parameterName));
  buf->write(parameterType);
  buf->write(std::string(infoName));
  buf->write(infoType);
  write(MessageType::GetParameterInfo, buf);

  std::unique_lock l(sync[SyncPoints::ParameterInfo].mtx);
  sync[SyncPoints::ParameterInfo].cv.wait(l,
      [this,
          &it,
          objectType,
          subtype,
          parameterName,
          parameterType,
          infoName,
          infoType]() {
        it = std::find_if(parameterInfos.begin(),
            parameterInfos.end(),
            [&it,
                objectType,
                subtype,
                parameterName,
                parameterType,
                infoName,
                infoType](const ParameterInfo::Ptr &pi) {
              return pi->objectType == objectType
                  && pi->objectSubtype == subtype
                  && pi->parameterName == std::string(parameterName)
                  && pi->parameterType == parameterType
                  && pi->info.name == std::string(infoName)
                  && pi->info.type == infoType;
            });
        return it != parameterInfos.end();
      });

  return (*it)->info.data();
}

//--- FrameBuffer Manipulation ------------------------

ANARIFrame Device::newFrame()
{
  return (ANARIFrame)registerNewObject(ANARI_FRAME);
}

const void *Device::frameBufferMap(ANARIFrame fb,
    const char *channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  if (!fb) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return nullptr;
  }

  Frame &frm = frames[fb];

  // this is a no-op if we already waited:
  frameReady(fb, ANARI_WAIT);

  *width = frm.size[0];
  *height = frm.size[1];

  if (std::string(channel) == "channel.color")
    *pixelType = frm.colorType;
  else if (std::string(channel) == "channel.depth")
    *pixelType = frm.depthType;

  frm.state = Frame::Mapped; // this needs to be done on a per-channel level!!

  if (std::string(channel) == "channel.color")
    return frm.color.data();
  else if (std::string(channel) == "channel.depth")
    return frm.depth.data();

  return nullptr;
}

void Device::frameBufferUnmap(ANARIFrame fb, const char *channel)
{
  if (!fb) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return;
  }

  Frame &frm = frames[fb];
  frm.state = Frame::Unmapped; // this needs to be done on a per-channel level!!
}

//--- Frame Rendering ---------------------------------

ANARIRenderer Device::newRenderer(const char *type)
{
  return (ANARIRenderer)registerNewObject(ANARI_RENDERER, type);
}

void Device::renderFrame(ANARIFrame frame)
{
  if (!frame) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return;
  }

  timing.beforeRenderFrame = getCurrentTime();

  LOG(logging::Level::Stats) << '\n';

  Frame &frm = frames[frame];

  // block till frame was unmapped
  std::unique_lock l(sync[SyncPoints::FrameIsReady].mtx);
  sync[SyncPoints::FrameIsReady].cv.wait(
      l, [&]() { return frm.state != Frame::Mapped; });
  l.unlock();

  auto buf = std::make_shared<Buffer>();
  buf->write(ObjectDesc(remoteDevice, frame));
  write(MessageType::RenderFrame, buf);
  frm.state = Frame::Render;
}

int Device::frameReady(ANARIFrame frame, ANARIWaitMask m)
{
  if (!frame) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return 0;
  }

  Frame &frm = frames[frame];

  if (m != ANARI_WAIT) {
    if (frm.frameID == 0)
      LOG(logging::Level::Warning)
          << "Performance: anariFrameReady() will wait";
    m = ANARI_WAIT;
  }

  if (frm.state == Frame::Render) {
    auto buf = std::make_shared<Buffer>();
    buf->write(ObjectDesc(remoteDevice, frame));
    buf->write(m);
    write(MessageType::FrameReady, buf);
    if (m == ANARI_WAIT) { // TODO: mask is probably a bitmask?!
      // block till device ID was returned by remote
      std::unique_lock l(sync[SyncPoints::FrameIsReady].mtx);
      sync[SyncPoints::FrameIsReady].cv.wait(
          l, [&]() { return frm.state == Frame::Ready; });
      l.unlock();

      timing.afterFrameReady = getCurrentTime();

      double t = timing.afterFrameReady - timing.beforeRenderFrame;
      LOG(logging::Level::Stats) << t << " sec. until frameReady";
      return true;
    }

    // TODO: implement for ANARI_NO_WAIT. Returns true
    // only when the frame was rendered
    return false;
  }

  return false;
}

void Device::discardFrame(ANARIFrame) {}

Device::Device(std::string subtype) : manager(async::make_connection_manager())
{
  logging::Initialize();

  char *serverHostName = getenv("ANARI_REMOTE_SERVER_HOSTNAME");
  if (serverHostName) {
    server.hostname = std::string(serverHostName);
    LOG(logging::Level::Info)
        << "Server hostname specified via environment: " << server.hostname;
  }

  char *serverPort = getenv("ANARI_REMOTE_SERVER_PORT");
  if (serverPort) {
    int p = std::stoi(serverPort);
    if (p >= 0 && p <= USHRT_MAX) {
      server.port = (unsigned short)p;
      LOG(logging::Level::Info)
          << "Server port specified via environment: " << server.port;
    } else {
      LOG(logging::Level::Warning)
          << "Server port specified via environment but ill-formed: " << p;
    }
  }

  remoteSubtype = subtype;
}

Device::~Device() {}

ANARIObject Device::registerNewObject(ANARIDataType type, std::string subtype)
{
  uint64_t objectID = nextObjectID++;
  ANARIObject object;
  memcpy(&object, &objectID, sizeof(objectID));

  ObjectDesc obj = makeObjectDesc(object);
  obj.type = type;
  obj.subtype = subtype;

  auto buf = std::make_shared<Buffer>();
  buf->write(obj);

  write(MessageType::NewObject, buf);

  LOG(logging::Level::Info)
      << "Object created: " << anari::toString(type) << ", sending "
      << prettyBytes(buf->size()) << ", objectID: " << objectID;

  return object;
}

ANARIArray Device::registerNewArray(ANARIDataType type,
    const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userPtr,
    const ANARIDataType elementType,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  uint64_t objectID = nextObjectID++;
  ANARIArray array;
  memcpy(&array, &objectID, sizeof(objectID));

  ObjectDesc obj = makeObjectDesc(array);
  obj.type = type;
  obj.subtype = "";

  auto buf = std::make_shared<Buffer>();
  buf->write(obj);
  buf->write(elementType);
  buf->write(numItems1);
  buf->write(numItems2);
  buf->write(numItems3);

  ArrayInfo info(type, elementType, numItems1, numItems2, numItems3);

  if (appMemory)
    buf->write((const char *)appMemory, info.getSizeInBytes());

  write(MessageType::NewArray, buf);

  LOG(logging::Level::Info)
      << "Array created: " << anari::toString(type) << ", sending "
      << prettyBytes(buf->size()) << ", objectID: " << objectID;

  return array;
}

ObjectDesc Device::makeObjectDesc(ANARIObject object) const
{
  if (object == (ANARIObject)this)
    object = remoteDevice;

  return ObjectDesc(remoteDevice, object);
}

void Device::initClient()
{
  connect(server.hostname, server.port);
  run();

  // wait till server accepted connection
  std::unique_lock l1(sync[SyncPoints::ConnectionEstablished].mtx);
  sync[SyncPoints::ConnectionEstablished].cv.wait(
      l1, [this]() { return conn; });
  l1.unlock();

  CompressionFeatures cf = getCompressionFeatures();

  // request remote device to be created, send other client info along
  auto buf = std::make_shared<Buffer>();
  buf->write(ObjectDesc{});
  buf->write(remoteSubtype);
  buf->write(cf);
  // write(MessageType::NewDevice, buf);
  //  post to queue directly: write() would call initClient() recursively!
  queue.post(std::bind(&Device::writeImpl, this, MessageType::NewDevice, buf));

  // block till device ID was returned by remote
  std::unique_lock l2(sync[SyncPoints::DeviceHandleRemote].mtx);
  sync[SyncPoints::DeviceHandleRemote].cv.wait(
      l2, [this]() { return remoteDevice; });
  l2.unlock();
}

void Device::connect(std::string host, unsigned short port)
{
  LOG(logging::Level::Info)
      << "ANARIDevice client connecting, host " << host << ", port " << port;

  manager->connect(host,
      port,
      std::bind(&Device::handleNewConnection,
          this,
          std::placeholders::_1,
          std::placeholders::_2));
}

void Device::run()
{
  LOG(logging::Level::Info) << "ANARIDevice client running in thread";
  manager->run_in_thread();
  queue.run_in_thread();
}

void Device::write(unsigned type, std::shared_ptr<Buffer> buf)
{
  if (!remoteDevice)
    initClient();

  queue.post(std::bind(&Device::writeImpl, this, type, buf));
}

void Device::write(unsigned type, const void *begin, const void *end)
{
  if (!remoteDevice)
    initClient();

  queue.post(std::bind(&Device::writeImpl2, this, type, begin, end));
}

bool Device::handleNewConnection(
    async::connection_pointer new_conn, boost::system::error_code const &e)
{
  if (e) {
    LOG(logging::Level::Error)
        << "ANARIDevice client: could not connect to server. Error: "
        << e.message();
    manager->stop();
    return false;
  }

  LOG(logging::Level::Info) << "ANARIDevice client: connected";

  // Accept and save this connection
  // and set the messange handler of the connection
  conn = new_conn;
  conn->set_handler(std::bind(&Device::handleMessage,
      this,
      std::placeholders::_1,
      std::placeholders::_2,
      std::placeholders::_3));

  // Notify main thread about the connection established
  sync[SyncPoints::ConnectionEstablished].cv.notify_all();

  return true;
}

void Device::handleMessage(async::connection::reason reason,
    async::message_pointer message,
    boost::system::error_code const &e)
{
  if (e) {
    LOG(logging::Level::Error) << "ANARIDevice client: error" << e.message();
    manager->stop();
    return;
  }

  if (reason == async::connection::Read) {
    if (message->type() == MessageType::DeviceHandle) {
      std::unique_lock l(sync[SyncPoints::DeviceHandleRemote].mtx);

      char *msg = message->data();

      remoteDevice = *(ANARIDevice *)msg;
      msg += sizeof(ANARIDevice);

      // Make sure that device and object handles are unique:
      nextObjectID = uint64_t(remoteDevice) + 1;

      server.compression = *(CompressionFeatures *)msg;
      msg += sizeof(CompressionFeatures);

      l.unlock();
      sync[SyncPoints::DeviceHandleRemote].cv.notify_all();
      LOG(logging::Level::Info) << "Got remote device handle: " << remoteDevice;
      LOG(logging::Level::Info)
          << "Server has TurboJPEG: " << server.compression.hasTurboJPEG;
      LOG(logging::Level::Info)
          << "Server has SNAPPY: " << server.compression.hasSNAPPY;
    } else if (message->type() == MessageType::ArrayMapped) {
      std::unique_lock l(sync[SyncPoints::MapArray].mtx);

      char *msg = message->data();

      ANARIArray arr = *(ANARIArray *)message->data();
      msg += sizeof(ANARIArray);
      uint64_t numBytes = 0;
      memcpy(&numBytes, msg, sizeof(numBytes));
      msg += sizeof(numBytes);

      arrays[arr].bytesExpected = numBytes;
      arrays[arr].value.resize(numBytes);
      memcpy(arrays[arr].value.data(), msg, numBytes);
      l.unlock();
      sync[SyncPoints::MapArray].cv.notify_all();
    } else if (message->type() == MessageType::ArrayUnmapped) {
      std::unique_lock l(sync[SyncPoints::UnmapArray].mtx);
      char *msg = message->data();
      ANARIArray arr = *(ANARIArray *)message->data();
      arrays[arr].bytesExpected = -1;
      arrays[arr].value.resize(0);
      l.unlock();
      sync[SyncPoints::MapArray].cv.notify_all();
    } else if (message->type() == MessageType::FrameIsReady) {
      assert(message->size() == sizeof(Handle));
      ANARIObject hnd = *(ANARIObject *)message->data();
      Frame &frm = frames[hnd];
      frm.state = Frame::Ready;
      frm.frameID++;
      sync[SyncPoints::FrameIsReady].cv.notify_all();
      // LOG(logging::Level::Info) << "Frame state: " << frameState;
    } else if (message->type() == MessageType::Property) {
      std::unique_lock l(sync[SyncPoints::Properties].mtx);

      Buffer buf(message->data(), message->size());

      Property prop;

      buf.read(prop.object);
      buf.read(prop.name);
      buf.read(prop.type);
      buf.read(prop.size);
      buf.read(prop.result);

      if (prop.type == ANARI_STRING) {
        std::string str;
        buf.read(str);
        prop.value.asAny = helium::AnariAny(prop.type, str.c_str());
      } else if (prop.type == ANARI_STRING_LIST) {
        buf.read(prop.value.asStringList);
      } else if (prop.type == ANARI_DATA_TYPE_LIST) {
        throw std::runtime_error(
            "getProperty with ANARI_DATA_TYPE_LIST not implemented yet!");
      } else { // POD
        if (!buf.eof()) {
          std::vector<char> bytes(anari::sizeOf(prop.type));
          buf.read(bytes.data(), bytes.size());
          prop.value.asAny = helium::AnariAny(prop.type, bytes.data());
        }
      }

      // Remove old property entry (if exists)
      auto it = std::find_if(properties.begin(),
          properties.end(),
          [prop](const Property &p) { return p.name == prop.name; });
      if (it != properties.end()) {
        properties.erase(it);
      }

      // Update list with new entry:
      properties.push_back(prop);

      l.unlock();
      sync[SyncPoints::Properties].cv.notify_all();

      // LOG(logging::Level::Info) << "Property: " << property.name;
    } else if (message->type() == MessageType::ObjectSubtypes) {
      std::unique_lock l(sync[SyncPoints::ObjectSubtypes].mtx);

      Buffer buf(message->data(), message->size());

      ObjectSubtypes os;

      buf.read(os.objectType);
      buf.read(os.value);

      objectSubtypes.push_back(os);

      l.unlock();
      sync[SyncPoints::ObjectSubtypes].cv.notify_all();
    } else if (message->type() == MessageType::ObjectInfo) {
      std::unique_lock l(sync[SyncPoints::ObjectInfo].mtx);

      Buffer buf(message->data(), message->size());

      auto oi = std::make_shared<ObjectInfo>();

      buf.read(oi->objectType);
      buf.read(oi->objectSubtype);
      buf.read(oi->info.name);
      buf.read(oi->info.type);
      if (oi->info.type == ANARI_STRING) {
        std::string str;
        buf.read(str);
        oi->info.value.asAny = helium::AnariAny(oi->info.type, str.c_str());
      } else if (oi->info.type == ANARI_STRING_LIST) {
        buf.read(oi->info.value.asStringList);
      } else if (oi->info.type == ANARI_PARAMETER_LIST) {
        buf.read(oi->info.value.asParameterList);
      } else {
        if (!buf.eof()) {
          std::vector<char> bytes(anari::sizeOf(oi->info.type));
          buf.read(bytes.data(), bytes.size());
          oi->info.value.asAny = helium::AnariAny(oi->info.type, bytes.data());
        }
      }

      objectInfos.push_back(oi);

      l.unlock();
      sync[SyncPoints::ObjectInfo].cv.notify_all();
    } else if (message->type() == MessageType::ParameterInfo) {
      std::unique_lock l(sync[SyncPoints::ParameterInfo].mtx);

      Buffer buf(message->data(), message->size());

      auto pi = std::make_shared<ParameterInfo>();

      buf.read(pi->objectType);
      buf.read(pi->objectSubtype);
      buf.read(pi->parameterName);
      buf.read(pi->parameterType);
      buf.read(pi->info.name);
      buf.read(pi->info.type);
      if (pi->info.type == ANARI_STRING) {
        std::string str;
        buf.read(str);
        pi->info.value.asAny = helium::AnariAny(pi->info.type, str.c_str());
      } else if (pi->info.type == ANARI_STRING_LIST) {
        buf.read(pi->info.value.asStringList);
      } else if (pi->info.type == ANARI_PARAMETER_LIST) {
        buf.read(pi->info.value.asParameterList);
      } else {
        if (!buf.eof()) {
          std::vector<char> bytes(anari::sizeOf(pi->info.type));
          buf.read(bytes.data(), bytes.size());
          pi->info.value.asAny = helium::AnariAny(pi->info.type, bytes.data());
        }
      }

      parameterInfos.push_back(pi);

      l.unlock();
      sync[SyncPoints::ParameterInfo].cv.notify_all();
    } else if (message->type() == MessageType::ChannelColor
        || message->type() == MessageType::ChannelDepth) {
      size_t off = 0;
      ANARIObject hnd = *(ANARIObject *)(message->data() + off);
      off += sizeof(Handle);

      uint32_t width, height;
      ANARIDataType type;

      width = *(uint32_t *)(message->data() + off);
      off += sizeof(width);

      height = *(uint32_t *)(message->data() + off);
      off += sizeof(height);

      type = *(uint32_t *)(message->data() + off);
      off += sizeof(type);

      Frame &frm = frames[hnd];

      timing.beforeFrameDecoded = getCurrentTime();

      double t = timing.beforeFrameDecoded - timing.beforeRenderFrame;
      std::string chan = message->type() == MessageType::ChannelColor
          ? "channel.color"
          : "channel.depth";
      LOG(logging::Level::Stats) << t << " sec. until " << chan << " received";

      CompressionFeatures cf = getCompressionFeatures();

      if (message->type() == MessageType::ChannelColor) {
        frm.resizeColor(width, height, type);

        bool compressionTurboJPEG =
            cf.hasTurboJPEG && server.compression.hasTurboJPEG;

        if (!compressionTurboJPEG && frm.frameID == 0) {
          if (cf.hasTurboJPEG)
            LOG(logging::Level::Warning)
                << "Performance: client supports TurboJPEG compression for colors, but server does not";
          else if (server.compression.hasTurboJPEG)
            LOG(logging::Level::Warning)
                << "Performance: server supports TurboJPEG compression for colors, but client does not";
          else
            LOG(logging::Level::Warning)
                << "Performance: neither client nor server support TurboJPEG compression for colors";
        }

        if (compressionTurboJPEG
            && type == ANARI_UFIXED8_RGBA_SRGB) { // TODO: more formats..
          uint32_t jpegSize = *(uint32_t *)(message->data() + off);
          off += sizeof(jpegSize);

          TurboJPEGOptions options;
          options.width = width;
          options.height = height;
          options.pixelFormat = TurboJPEGOptions::PixelFormat::RGBX;

          if (uncompressTurboJPEG((const uint8_t *)message->data() + off,
                  frm.color.data(),
                  jpegSize,
                  options)) {
            LOG(logging::Level::Stats)
                << "TurboJPEG: raw " << prettyBytes(frm.color.size())
                << ", compressed: " << prettyBytes(jpegSize)
                << ", rate: " << double(frm.color.size()) / jpegSize;
          }
        } else {
          size_t numBytes = width * height * anari::sizeOf(type);
          memcpy(frm.color.data(), message->data() + off, numBytes);
        }
      } else {
        frm.resizeDepth(width, height, type);

        bool compressionSNAPPY = cf.hasSNAPPY && server.compression.hasSNAPPY;

        if (!compressionSNAPPY && frm.frameID == 0) {
          if (cf.hasTurboJPEG)
            LOG(logging::Level::Warning)
                << "Performance: client supports SNAPPY compression for depths, but server does not";
          else if (server.compression.hasSNAPPY)
            LOG(logging::Level::Warning)
                << "Performance: server supports SNAPPY compression for depths, but client does not";
          else
            LOG(logging::Level::Warning)
                << "Performance: neither client nor server support SNAPPY compression for depths";
        }

        if (compressionSNAPPY && type == ANARI_FLOAT32) {
          uint32_t snappySize = *(uint32_t *)(message->data() + off);
          off += sizeof(snappySize);

          if (uncompressSNAPPY((const uint8_t *)message->data() + off,
                  frm.depth.data(),
                  snappySize,
                  SNAPPYOptions{})) {
            LOG(logging::Level::Stats)
                << "SNAPPY: raw " << prettyBytes(frm.depth.size())
                << ", compressed: " << prettyBytes(snappySize)
                << ", rate: " << double(frm.depth.size()) / snappySize;
          } else {
            LOG(logging::Level::Warning) << "snappy::RawUncompress failed";
          }
        } else {
          size_t numBytes = width * height * anari::sizeOf(type);
          memcpy(frm.depth.data(), message->data() + off, numBytes);
        }
      }

      timing.afterFrameDecoded = getCurrentTime();

      t = timing.afterFrameDecoded - timing.beforeFrameDecoded;
      double t_total = timing.afterFrameDecoded - timing.beforeRenderFrame;
      LOG(logging::Level::Stats) << t << " sec. to decode " << chan;
      LOG(logging::Level::Stats)
          << t_total << " sec. total until " << chan << " ready to use";
    } else {
      LOG(logging::Level::Warning)
          << "Unhandled message of size: " << message->size();
    }
  } else {
  }
}

void Device::writeImpl(unsigned type, std::shared_ptr<Buffer> buf)
{
  conn->write(type, *buf);
}

void Device::writeImpl2(unsigned type, const void *begin, const void *end)
{
  conn->write(type, (const char *)begin, (const char *)end);
}

} // namespace remote
