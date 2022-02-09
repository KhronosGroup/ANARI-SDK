// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari.h"

namespace anari {
namespace debug_device {

class DebugInterface {
public:
    virtual ~DebugInterface() = default;
    virtual void anariDeviceImplements(ANARIDevice device, const char* profile) = 0;
    virtual void anariNewArray1D(ANARIDevice device, void* appMemory, ANARIMemoryDeleter deleter, void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t byteStride1) = 0;
    virtual void anariNewArray2D(ANARIDevice device, void* appMemory, ANARIMemoryDeleter deleter, void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t byteStride1, uint64_t byteStride2) = 0;
    virtual void anariNewArray3D(ANARIDevice device, void* appMemory, ANARIMemoryDeleter deleter, void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t numItems3, uint64_t byteStride1, uint64_t byteStride2, uint64_t byteStride3) = 0;
    virtual void anariMapArray(ANARIDevice device, ANARIArray array) = 0;
    virtual void anariUnmapArray(ANARIDevice device, ANARIArray array) = 0;
    virtual void anariNewLight(ANARIDevice device, const char* type) = 0;
    virtual void anariNewCamera(ANARIDevice device, const char* type) = 0;
    virtual void anariNewGeometry(ANARIDevice device, const char* type) = 0;
    virtual void anariNewSpatialField(ANARIDevice device, const char* type) = 0;
    virtual void anariNewVolume(ANARIDevice device, const char* type) = 0;
    virtual void anariNewSurface(ANARIDevice device) = 0;
    virtual void anariNewMaterial(ANARIDevice device, const char* type) = 0;
    virtual void anariNewSampler(ANARIDevice device, const char* type) = 0;
    virtual void anariNewGroup(ANARIDevice device) = 0;
    virtual void anariNewInstance(ANARIDevice device) = 0;
    virtual void anariNewWorld(ANARIDevice device) = 0;
    virtual void anariNewObject(ANARIDevice device, const char* objectType, const char* type) = 0;
    virtual void anariSetParameter(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, const void *mem) = 0;
    virtual void anariUnsetParameter(ANARIDevice device, ANARIObject object, const char* name) = 0;
    virtual void anariCommit(ANARIDevice device, ANARIObject object) = 0;
    virtual void anariRelease(ANARIDevice device, ANARIObject object) = 0;
    virtual void anariRetain(ANARIDevice device, ANARIObject object) = 0;
    virtual void anariGetProperty(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType type, void* mem, uint64_t size, ANARIWaitMask mask) = 0;
    virtual void anariNewFrame(ANARIDevice device) = 0;
    virtual void anariMapFrame(ANARIDevice device, ANARIFrame frame, const char* channel) = 0;
    virtual void anariUnmapFrame(ANARIDevice device, ANARIFrame frame, const char* channel) = 0;
    virtual void anariNewRenderer(ANARIDevice device, const char* type) = 0;
    virtual void anariRenderFrame(ANARIDevice device, ANARIFrame frame) = 0;
    virtual void anariFrameReady(ANARIDevice device, ANARIFrame frame, ANARIWaitMask mask) = 0;
    virtual void anariDiscardFrame(ANARIDevice device, ANARIFrame frame) = 0;
    virtual void anariReleaseDevice(ANARIDevice device) = 0;
};

}
}
