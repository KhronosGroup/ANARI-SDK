//  Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Device.h"
#include <anari/backend/LibraryImpl.h>
#include <anari/anari_cpp.hpp>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include "ArrayInfo.h"
#include "Compression.h"
#include "Frame.h"
#include "Logging.h"
#include "async/connection.h"
#include "async/connection_manager.h"
#include "async/work_queue.h"
#include "common.h"

#ifndef _WIN32
#include <sys/time.h>
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

ANARIArray1D Device::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userPtr,
    const ANARIDataType elementType,
    uint64_t numItems1,
    uint64_t byteStride1)
{
  return (ANARIArray1D)registerNewArray(ANARI_ARRAY1D,
      appMemory,
      deleter,
      userPtr,
      elementType,
      numItems1,
      0,
      0,
      byteStride1,
      0,
      0);
}

ANARIArray2D Device::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userPtr,
    ANARIDataType elementType,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1,
    uint64_t byteStride2)
{
  return (ANARIArray2D)registerNewArray(ANARI_ARRAY2D,
      appMemory,
      deleter,
      userPtr,
      elementType,
      numItems1,
      numItems2,
      0,
      byteStride1,
      byteStride2,
      0);
}

ANARIArray3D Device::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userPtr,
    ANARIDataType elementType,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
{
  return (ANARIArray3D)registerNewArray(ANARI_ARRAY3D,
      appMemory,
      deleter,
      userPtr,
      elementType,
      numItems1,
      numItems2,
      numItems3,
      byteStride1,
      byteStride2,
      byteStride3);
}

void *Device::mapArray(ANARIArray)
{
  return nullptr;
}

void Device::unmapArray(ANARIArray) {}

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

ANARIInstance Device::newInstance()
{
  return (ANARIInstance)registerNewObject(ANARI_INSTANCE);
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

  std::vector<char> value;
  if (anari::isObject(type)) {
    value.resize(sizeof(uint64_t));
    memcpy(value.data(), mem, sizeof(uint64_t));
  } else {
    value.resize(anari::sizeOf(type));
    memcpy(value.data(), mem, anari::sizeOf(type));
  }

  auto buf = std::make_shared<Buffer>();
  buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
  buf->write((const char *)&object, sizeof(object));
  uint64_t nameLen = strlen(name);
  buf->write((const char *)&nameLen, sizeof(nameLen));
  buf->write(name, nameLen);
  buf->write((const char *)&type, sizeof(type));
  buf->write(value.data(), value.size());
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
  buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
  buf->write((const char *)&object, sizeof(object));
  uint64_t nameLen = strlen(name);
  buf->write((const char *)&nameLen, sizeof(nameLen));
  buf->write(name, nameLen);
  write(MessageType::UnsetParam, buf);

  LOG(logging::Level::Info)
      << "Parameter " << name << " unset on object " << object;
}

void Device::commitParameters(ANARIObject object)
{
  if (!object) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return;
  }

  if (object
      == (ANARIObject)this) { // TODO: what happens if we actually assign our
                              // pointer value via nextObjectID++ ??? :-D
    auto buf = std::make_shared<Buffer>();
    buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
    write(
        MessageType::CommitParams, buf); // one handle only: commit the device!
  } else {
    auto buf = std::make_shared<Buffer>();
    buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
    buf->write((const char *)&object, sizeof(object));
    write(MessageType::CommitParams, buf);

    LOG(logging::Level::Info) << "Parameters committed on object " << object;
  }
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
  buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
  buf->write((const char *)&object, sizeof(object));
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
  buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
  buf->write((const char *)&object, sizeof(object));
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
  buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
  buf->write((const char *)&object, sizeof(object));
  uint64_t len = strlen(name);
  buf->write((const char *)&len, sizeof(len));
  buf->write(name, len);
  buf->write((const char *)&type, sizeof(type));
  buf->write((const char *)&size, sizeof(size));
  buf->write((const char *)&mask, sizeof(mask));
  write(MessageType::GetProperty, buf);

  std::unique_lock l(syncProperties.mtx);
  property.object = nullptr;
  property.type = type;
  property.size = size;
  syncProperties.cv.wait(l, [&, this]() { return property.object; });

  if (type == ANARI_STRING_LIST) {
    auto it = std::find_if(stringListProperties.begin(),
        stringListProperties.end(),
        [name](const StringListProperty &prop) {
          return prop.name == std::string(name);
        });
    if (it != stringListProperties.end()) {
      writeToVoidP(mem, it->value.data());
    }
  } else if (type == ANARI_DATA_TYPE_LIST) {
    throw std::runtime_error(
        "getProperty with ANARI_DATA_TYPE_LIST not implemented yet!");
  } else { // POD
    memcpy(mem, property.mem.data(), size);
  }
  int result = property.result;
  l.unlock();

  return result;
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

  if (strncmp(channel, "channel.color", 13) == 0)
    *pixelType = frm.colorType;
  else if (strncmp(channel, "channel.depth", 13) == 0)
    *pixelType = frm.depthType;

  frm.state = Frame::Mapped; // this needs to be done on a per-channel level!!

  if (strncmp(channel, "channel.color", 13) == 0)
    return frm.color.data();
  else if (strncmp(channel, "channel.depth", 13) == 0)
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
  std::unique_lock l(syncFrameIsReady.mtx);
  syncFrameIsReady.cv.wait(l, [&]() { return frm.state != Frame::Mapped; });
  l.unlock();

  auto buf = std::make_shared<Buffer>();
  buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
  buf->write((const char *)&frame, sizeof(frame));
  write(MessageType::RenderFrame, buf);
  frm.state = Frame::Render;
}

int Device::frameReady(ANARIFrame frame, ANARIWaitMask m)
{
  if (m != ANARI_WAIT) {
    LOG(logging::Level::Warning) << "Performance: anariFrameReady() will wait";
    m = ANARI_WAIT;
  }

  if (!frame) {
    LOG(logging::Level::Warning) << "Invalid object: " << __PRETTY_FUNCTION__;
    return 0;
  }

  Frame &frm = frames[frame];

  if (frm.state == Frame::Render) {
    auto buf = std::make_shared<Buffer>();
    buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
    buf->write((const char *)&frame, sizeof(frame));
    buf->write((const char *)&m, sizeof(m));
    write(MessageType::FrameReady, buf);
    if (m == ANARI_WAIT) { // TODO: mask is probably a bitmask?!
      // block till device ID was returned by remote
      std::unique_lock l(syncFrameIsReady.mtx);
      syncFrameIsReady.cv.wait(l, [&]() { return frm.state == Frame::Ready; });
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

  remoteSubtype = subtype;
}

Device::~Device()
{
  for (size_t i = 0; i < stringListProperties.size(); ++i) {
    for (size_t j = 0; j < stringListProperties[i].value.size(); ++j) {
      delete[] stringListProperties[i].value[j];
    }
  }
}

ANARIObject Device::registerNewObject(ANARIDataType type, std::string subtype)
{
  uint64_t objectID = nextObjectID++;
  ANARIObject object;
  memcpy(&object, &objectID, sizeof(objectID));

  auto buf = std::make_shared<Buffer>();

  buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
  buf->write((const char *)&type, sizeof(type));
  uint64_t len = subtype.length();
  buf->write((const char *)&len, sizeof(len));
  buf->write(subtype.c_str(), subtype.length());
  buf->write((const char *)&objectID, sizeof(objectID));

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
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
{
  uint64_t objectID = nextObjectID++;
  ANARIArray array;
  memcpy(&array, &objectID, sizeof(objectID));

  auto buf = std::make_shared<Buffer>();
  buf->write((const char *)&remoteDevice, sizeof(remoteDevice));
  buf->write((const char *)&type, sizeof(type));
  buf->write((const char *)&objectID, sizeof(objectID));
  buf->write((const char *)&elementType, sizeof(elementType));
  buf->write((const char *)&numItems1, sizeof(numItems1));
  buf->write((const char *)&numItems2, sizeof(numItems2));
  buf->write((const char *)&numItems3, sizeof(numItems3));
  buf->write((const char *)&byteStride1, sizeof(byteStride1));
  buf->write((const char *)&byteStride2, sizeof(byteStride2));
  buf->write((const char *)&byteStride3, sizeof(byteStride3));

  ArrayInfo info(type,
      elementType,
      numItems1,
      numItems2,
      numItems3,
      byteStride1,
      byteStride2,
      byteStride3);

  if (appMemory)
    buf->write((const char *)appMemory, info.getSizeInBytes());

  write(MessageType::NewArray, buf);

  LOG(logging::Level::Info)
      << "Array created: " << anari::toString(type) << ", sending "
      << prettyBytes(buf->size()) << ", objectID: " << objectID;

  return array;
}

void Device::initClient()
{
  connect("localhost", 31050);
  run();

  // wait till server accepted connection
  std::unique_lock l1(syncConnectionEstablished.mtx);
  syncConnectionEstablished.cv.wait(l1, [this]() { return conn; });
  l1.unlock();

  // request remote device to be created
  auto buf = std::make_shared<Buffer>();
  buf->write(remoteSubtype.c_str(), remoteSubtype.length());
  //write(MessageType::NewDevice, buf);
  // post to queue directly: write() would call initClient() recursively!
  queue.post(std::bind(&Device::writeImpl, this, MessageType::NewDevice, buf));

  // block till device ID was returned by remote
  std::unique_lock l2(syncDeviceHandleRemote.mtx);
  syncDeviceHandleRemote.cv.wait(l2, [this]() { return remoteDevice; });
  l2.unlock();
}

void Device::connect(std::string host, unsigned short port)
{
  LOG(logging::Level::Info)
      << "ANARIDevice client connecting, host " << host << ", port " << port;

  manager->connect(
      host, port, std::bind(&Device::handleNewConnection, this, _1, _2));
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
  conn->set_handler(std::bind(&Device::handleMessage, this, _1, _2, _3));

  // Notify main thread about the connection established
  syncConnectionEstablished.cv.notify_all();

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
      assert(message->size() == sizeof(Handle));
      std::unique_lock l(syncDeviceHandleRemote.mtx);
      remoteDevice = *(ANARIDevice *)message->data();
      l.unlock();
      syncDeviceHandleRemote.cv.notify_all();
      LOG(logging::Level::Info) << "Got remote device handle: " << remoteDevice;
    } else if (message->type() == MessageType::FrameIsReady) {
      assert(message->size() == sizeof(Handle));
      ANARIObject hnd = *(ANARIObject *)message->data();
      Frame &frm = frames[hnd];
      frm.state = Frame::Ready;
      syncFrameIsReady.cv.notify_all();
      // LOG(logging::Level::Info) << "Frame state: " << frameState;
    } else if (message->type() == MessageType::Property) {
      std::unique_lock l(syncProperties.mtx);

      Buffer buf;
      buf.write(message->data(), message->size());
      buf.seek(0);

      ANARIObject handle;
      buf.read((char *)&handle, sizeof(handle));

      property.object = handle;

      uint64_t len;
      buf.read((char *)&len, sizeof(len));

      std::vector<char> name(len);
      buf.read(name.data(), len);
      name[len] = '\0'; // TODO: check...
      property.name = std::string(name.data(), name.size());

      buf.read((char *)&property.result, sizeof(property.result));

      if (property.type == ANARI_STRING_LIST) {
        // Delete old list (if exists)
        auto it = std::find_if(stringListProperties.begin(),
            stringListProperties.end(),
            [name](const StringListProperty &prop) {
              return prop.name == std::string(name.data());
            });
        if (it != stringListProperties.end()) {
          for (size_t i = 0; i < it->value.size(); ++i) {
            delete[] it->value[i];
          }
        }

        StringListProperty prop;

        prop.object = handle;
        prop.name = std::string(name.data(), name.size());

        uint64_t listSize;
        buf.read((char *)&listSize, sizeof(listSize));

        prop.value.resize(listSize + 1);
        prop.value[listSize] = nullptr;

        for (uint64_t i = 0; i < listSize; ++i) {
          uint64_t strLen;
          buf.read((char *)&strLen, sizeof(strLen));

          prop.value[i] = new char[strLen + 1];
          buf.read(prop.value[i], strLen);
          prop.value[i][strLen] = '\0';
        }

        stringListProperties.push_back(prop);
      } else if (property.type == ANARI_DATA_TYPE_LIST) {
        throw std::runtime_error(
            "getProperty with ANARI_DATA_TYPE_LIST not implemented yet!");
      } else { // POD
        property.mem.resize(property.size);
        buf.read(property.mem.data(), property.size);
      }

      l.unlock();
      syncProperties.cv.notify_all();

      // LOG(logging::Level::Info) << "Property: " << property.name;
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

      if (message->type() == MessageType::ChannelColor) {
        frm.resizeColor(width, height, type);

#ifdef HAVE_TURBOJPEG
        if (type == ANARI_UFIXED8_RGBA_SRGB) { // TODO: more formats..
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
        } else
#endif
        {
          size_t numBytes = width * height * anari::sizeOf(type);
          memcpy(frm.color.data(), message->data() + off, numBytes);
        }
      } else {
        frm.resizeDepth(width, height, type);

#ifdef HAVE_SNAPPY
        if (type == ANARI_FLOAT32) {
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
        } else
#endif
        {
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

static char deviceName[] = "remote";

extern "C" ANARI_DEFINE_LIBRARY_NEW_DEVICE(remote, library, subtype)
{
  return (ANARIDevice) new remote::Device(subtype);
}

extern "C" ANARI_DEFINE_LIBRARY_INIT(remote)
{
  printf("...loaded remote library!\n");
}

extern "C" ANARI_DEFINE_LIBRARY_GET_DEVICE_SUBTYPES(remote, libdata)
{
  static const char *devices[] = {deviceName, nullptr};
  return devices;
}

extern "C" ANARI_DEFINE_LIBRARY_GET_OBJECT_SUBTYPES(
    remote, libdata, deviceSubtype, objectType)
{
  return nullptr;
}

extern "C" ANARI_DEFINE_LIBRARY_GET_OBJECT_PROPERTY(remote,
    library,
    deviceSubtype,
    objectSubtype,
    objectType,
    propertyName,
    propertyType)
{
  if (propertyType == ANARI_RENDERER) {
    // return remote::Renderer::Parameters;
  }
  return nullptr;
}

extern "C" ANARI_DEFINE_LIBRARY_GET_PARAMETER_PROPERTY(remote,
    libdata,
    deviceSubtype,
    objectSubtype,
    objectType,
    parameterName,
    parameterType,
    propertyName,
    propertyType)
{
  return nullptr;
}

extern "C" ANARIDevice anariNewRemoteDevice()
{
  return (ANARIDevice) new remote::Device;
}
