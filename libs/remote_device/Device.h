// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/backend/DeviceImpl.h>
#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>
#include "Buffer.h"
#include "Compression.h"
#include "Frame.h"
#include "ParameterList.h"
#include "StringList.h"
#include "async/connection.h"
#include "async/connection_manager.h"
#include "async/work_queue.h"
#include "utility/AnariAny.h"
#include "utility/IntrusivePtr.h"
#include "utility/ParameterizedObject.h"

namespace remote {

struct Device : anari::DeviceImpl, helium::ParameterizedObject
{
  //--- Data Arrays ---------------------------------

  void *mapParameterArray1D(ANARIObject o,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t *elementStride) override;
  void *mapParameterArray2D(ANARIObject o,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t *elementStride) override;
  void *mapParameterArray3D(ANARIObject o,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t numElements3,
      uint64_t *elementStride) override;
  void unmapParameterArray(ANARIObject o, const char *name) override;

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

  void *mapArray(ANARIArray) override;

  void unmapArray(ANARIArray) override;

  //--- Renderable Objects --------------------------

  ANARILight newLight(const char *type) override;

  ANARICamera newCamera(const char *type) override;

  ANARIGeometry newGeometry(const char *type) override;

  ANARISpatialField newSpatialField(const char *type) override;

  ANARISurface newSurface() override;

  ANARIVolume newVolume(const char *type) override;

  //--- Surface Meta-Data ---------------------------

  ANARIMaterial newMaterial(const char *material_type) override;

  ANARISampler newSampler(const char *type) override;

  //--- Instancing ----------------------------------

  ANARIGroup newGroup() override;

  ANARIInstance newInstance(const char *type) override;

  //--- Top-level Worlds ----------------------------

  ANARIWorld newWorld() override;

  //--- Object + Parameter Lifetime Management ------

  void setParameter(ANARIObject object,
      const char *name,
      ANARIDataType type,
      const void *mem) override;

  void unsetParameter(ANARIObject object, const char *name) override;

  void unsetAllParameters(ANARIObject object) override;

  void commitParameters(ANARIObject object) override;

  void release(ANARIObject _obj) override;

  void retain(ANARIObject _obj) override;

  //--- Object Query Interface ----------------------

  int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask) override;

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

  //--- FrameBuffer Manipulation --------------------

  ANARIFrame newFrame() override;

  const void *frameBufferMap(ANARIFrame fb,
      const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;

  void frameBufferUnmap(ANARIFrame fb, const char *channel) override;

  //--- Frame Rendering -----------------------------

  ANARIRenderer newRenderer(const char *type) override;

  void renderFrame(ANARIFrame) override;

  int frameReady(ANARIFrame, ANARIWaitMask) override;

  void discardFrame(ANARIFrame) override;

 public:
  Device(std::string subtype = "default");
  ~Device();

 private:
  void initClient();
  uint64_t nextObjectID = 1;

  struct
  {
    std::string hostname = "localhost";
    unsigned short port{31050};
    CompressionFeatures compression;
  } server;

  async::connection_manager_pointer manager;
  async::connection_pointer conn;
  async::work_queue queue;

  struct SyncPrimitives
  {
    std::mutex mtx;
    std::condition_variable cv;
  };

  struct SyncPoints
  {
    enum
    {
      ConnectionEstablished,
      DeviceHandleRemote,
      MapArray,
      UnmapArray,
      FrameIsReady,
      Properties,
      ObjectSubtypes,
      ObjectInfo,
      ParameterInfo,
      // Keep last:
      Count,
    };
  };

  SyncPrimitives sync[SyncPoints::Count];

  ANARIDevice remoteDevice{nullptr};
  std::string remoteSubtype = "default";

  // getProperty blocks until property.object is
  // not null, then consumes the remaining values
  struct
  {
    ANARIObject object{nullptr};
    std::string name;
    std::vector<char> mem;
    ANARIDataType type;
    uint64_t size;
    int result;
  } property;

  // Cache string list properties and keep strings
  // alive for the device's lifetime!
  struct StringListProperty
  {
    ANARIObject object{nullptr};
    std::string name;
    StringList value;
  };
  std::vector<StringListProperty> stringListProperties;

  // Store subtypes; if the subtype was already queried,
  // simply return the cached subtype list
  struct ObjectSubtypes
  {
    ANARIDataType objectType;
    StringList value;
  };
  std::vector<ObjectSubtypes> objectSubtypes;

  struct Info
  {
    std::string name;
    ANARIDataType type;
    helium::AnariAny asAny;
    StringList asStringList;
    remote::ParameterList asParameterList;

    const void *data() const
    {
      if (type == ANARI_STRING_LIST)
        return asStringList.data();
      else if (type == ANARI_PARAMETER_LIST)
        return asParameterList.data();
      else if (asAny.type() != ANARI_UNKNOWN)
        return asAny.data();
      else
        return nullptr;
    }
  };

  // Cache for "object infos"
  struct ObjectInfo
  {
    typedef std::shared_ptr<ObjectInfo> Ptr;
    ANARIDataType objectType;
    std::string objectSubtype;
    std::string infoName;
    Info info;
  };
  std::vector<ObjectInfo::Ptr> objectInfos;

  // Cache for "parameter infos"
  struct ParameterInfo
  {
    typedef std::shared_ptr<ParameterInfo> Ptr;
    ANARIDataType objectType;
    std::string objectSubtype;
    std::string parameterName;
    ANARIDataType parameterType;
    Info info;
  };
  std::vector<ParameterInfo::Ptr> parameterInfos;

  std::map<ANARIObject, Frame> frames;
  struct ArrayData
  {
    ssize_t bytesExpected{-1};
    std::vector<char> value;
  };
  std::map<ANARIArray, ArrayData> arrays;

  // Need to keep track of these to implement
  // (un)mapParameterArray correctly
  struct ParameterArray
  {
    ANARIObject object{nullptr};
    const char *name = "";
    bool operator<(const ParameterArray &other) const
    {
      return object && object == other.object && strlen(name) > 0
          && std::string(name) < std::string(other.name);
    }
  };
  std::map<ParameterArray, ANARIArray> parameterArrays;

  ANARIObject registerNewObject(ANARIDataType type, std::string subtype = "");
  ANARIArray registerNewArray(ANARIDataType type,
      const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      const ANARIDataType elementType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3);

  //--- Net ---------------------------------------------
  void connect(std::string host, unsigned short port);

  void run();

  void write(unsigned type, std::shared_ptr<Buffer> buf);
  void write(unsigned type, const void *begin, const void *end);

  bool handleNewConnection(
      async::connection_pointer new_conn, boost::system::error_code const &e);

  void handleMessage(async::connection::reason reason,
      async::message_pointer message,
      boost::system::error_code const &e);

  void writeImpl(unsigned type, std::shared_ptr<Buffer> buf);
  void writeImpl2(unsigned type, const void *begin, const void *end);

  //--- Stats -------------------------------------------
  struct
  {
    double beforeRenderFrame = 0.0;
    double afterFrameReady = 0.0;
    double beforeFrameDecoded = 0.0;
    double afterFrameDecoded = 0.0;
  } timing;
};

} // namespace remote
