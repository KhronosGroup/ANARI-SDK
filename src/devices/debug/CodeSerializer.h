// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari.h"
#include "DebugDevice.h"
#include <fstream>

namespace anari {
namespace debug_device {

class CodeSerializer : public SerializerInterface {
   DebugDevice *dd;
   std::ofstream out;
   std::ofstream data;
   uint64_t locals;
public:
   CodeSerializer(DebugDevice *dd);
   void printObjectName(ANARIObject);
   void anariNewArray1D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, ANARIArray1D result) override;
   void anariNewArray2D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, ANARIArray2D result) override;
   void anariNewArray3D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t numItems3, ANARIArray3D result) override;
   void anariMapArray(ANARIDevice device, ANARIArray array, void *result) override;
   void anariUnmapArray(ANARIDevice device, ANARIArray array) override;
   void anariNewLight(ANARIDevice device, const char* type, ANARILight result) override;
   void anariNewCamera(ANARIDevice device, const char* type, ANARICamera result) override;
   void anariNewGeometry(ANARIDevice device, const char* type, ANARIGeometry result) override;
   void anariNewSpatialField(ANARIDevice device, const char* type, ANARISpatialField result) override;
   void anariNewVolume(ANARIDevice device, const char* type, ANARIVolume result) override;
   void anariNewSurface(ANARIDevice device, ANARISurface result) override;
   void anariNewMaterial(ANARIDevice device, const char* type, ANARIMaterial result) override;
   void anariNewSampler(ANARIDevice device, const char* type, ANARISampler result) override;
   void anariNewGroup(ANARIDevice device, ANARIGroup result) override;
   void anariNewInstance(ANARIDevice device, const char *type, ANARIInstance result) override;
   void anariNewWorld(ANARIDevice device, ANARIWorld result) override;
   void anariNewObject(ANARIDevice device, const char* objectType, const char* type, ANARIObject result) override;
   void anariSetParameter(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, const void *mem) override;
   void anariUnsetParameter(ANARIDevice device, ANARIObject object, const char* name) override;
   void anariUnsetAllParameters(ANARIDevice device, ANARIObject object) override;

   void anariMapParameterArray1D(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, uint64_t numElements1, uint64_t *elementStride, void *result) override;
   void anariMapParameterArray2D(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, uint64_t numElements1, uint64_t numElements2, uint64_t *elementStride, void *result) override;
   void anariMapParameterArray3D(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, uint64_t numElements1, uint64_t numElements2, uint64_t numElements3, uint64_t *elementStride, void *result) override;
   void anariUnmapParameterArray(ANARIDevice device, ANARIObject object, const char* name) override;

   void anariCommitParameters(ANARIDevice device, ANARIObject object) override;
   void anariRelease(ANARIDevice device, ANARIObject object) override;
   void anariRetain(ANARIDevice device, ANARIObject object) override;
   void anariGetProperty(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType type, void* mem, uint64_t size, ANARIWaitMask mask, int result) override;
   void anariNewFrame(ANARIDevice device, ANARIFrame result) override;
   void anariMapFrame(ANARIDevice device, ANARIFrame frame, const char* channel, uint32_t *width, uint32_t *height, ANARIDataType *pixelType, const void *mapped) override;
   void anariUnmapFrame(ANARIDevice device, ANARIFrame frame, const char* channel) override;
   void anariNewRenderer(ANARIDevice device, const char* type, ANARIRenderer result) override;
   void anariRenderFrame(ANARIDevice device, ANARIFrame frame) override;
   void anariFrameReady(ANARIDevice device, ANARIFrame frame, ANARIWaitMask mask, int result) override;
   void anariDiscardFrame(ANARIDevice device, ANARIFrame frame) override;
   void anariReleaseDevice(ANARIDevice device) override;
   void insertStatus(ANARIObject source, ANARIDataType sourceType, ANARIStatusSeverity severity, ANARIStatusCode code, const char *status) override;

   static SerializerInterface* create(DebugDevice*);
};


}
}