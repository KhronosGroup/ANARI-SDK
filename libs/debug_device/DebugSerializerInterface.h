// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari.h"

namespace anari {
namespace debug_device {

class SerializerInterface {
public:
    virtual ~SerializerInterface() = default;
    virtual void anariNewArray1D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t byteStride1, ANARIArray1D result) = 0;
    virtual void anariNewArray2D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t byteStride1, uint64_t byteStride2, ANARIArray2D result) = 0;
    virtual void anariNewArray3D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t numItems3, uint64_t byteStride1, uint64_t byteStride2, uint64_t byteStride3, ANARIArray3D result) = 0;
    virtual void anariMapArray(ANARIDevice device, ANARIArray array, void *result) = 0;
    virtual void anariUnmapArray(ANARIDevice device, ANARIArray array) = 0;
    virtual void anariNewLight(ANARIDevice device, const char* type, ANARILight result) = 0;
    virtual void anariNewCamera(ANARIDevice device, const char* type, ANARICamera result) = 0;
    virtual void anariNewGeometry(ANARIDevice device, const char* type, ANARIGeometry result) = 0;
    virtual void anariNewSpatialField(ANARIDevice device, const char* type, ANARISpatialField result) = 0;
    virtual void anariNewVolume(ANARIDevice device, const char* type, ANARIVolume result) = 0;
    virtual void anariNewSurface(ANARIDevice device, ANARISurface result) = 0;
    virtual void anariNewMaterial(ANARIDevice device, const char* type, ANARIMaterial result) = 0;
    virtual void anariNewSampler(ANARIDevice device, const char* type, ANARISampler result) = 0;
    virtual void anariNewGroup(ANARIDevice device, ANARIGroup result) = 0;
    virtual void anariNewInstance(ANARIDevice device, ANARIInstance result) = 0;
    virtual void anariNewWorld(ANARIDevice device, ANARIWorld result) = 0;
    virtual void anariNewObject(ANARIDevice device, const char* objectType, const char* type, ANARIObject result) = 0;
    virtual void anariSetParameter(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, const void *mem) = 0;
    virtual void anariUnsetParameter(ANARIDevice device, ANARIObject object, const char* name) = 0;
    virtual void anariCommitParameters(ANARIDevice device, ANARIObject object) = 0;
    virtual void anariRelease(ANARIDevice device, ANARIObject object) = 0;
    virtual void anariRetain(ANARIDevice device, ANARIObject object) = 0;
    virtual void anariGetProperty(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType type, void* mem, uint64_t size, ANARIWaitMask mask, int result) = 0;
    virtual void anariNewFrame(ANARIDevice device, ANARIFrame result) = 0;
    virtual void anariMapFrame(ANARIDevice device, ANARIFrame frame, const char* channel, const void *mapped) = 0;
    virtual void anariUnmapFrame(ANARIDevice device, ANARIFrame frame, const char* channel) = 0;
    virtual void anariNewRenderer(ANARIDevice device, const char* type, ANARIRenderer result) = 0;
    virtual void anariRenderFrame(ANARIDevice device, ANARIFrame frame) = 0;
    virtual void anariFrameReady(ANARIDevice device, ANARIFrame frame, ANARIWaitMask mask, int result) = 0;
    virtual void anariDiscardFrame(ANARIDevice device, ANARIFrame frame) = 0;
    virtual void anariReleaseDevice(ANARIDevice device) = 0;
    virtual void insertStatus(ANARIObject source, ANARIDataType sourceType, ANARIStatusSeverity severity, ANARIStatusCode code, const char *status) = 0;
};

}
}
