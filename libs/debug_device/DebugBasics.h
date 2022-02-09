// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DebugInterface.h"
#include "DebugDevice.h"

namespace anari {
namespace debug_device {

struct DebugDevice;

class DebugBasics : public DebugInterface {
    DebugDevice *td;

public:
    DebugBasics(DebugDevice *td);
    void anariDeviceImplements(ANARIDevice device, const char* profile) override;
    void anariNewArray1D(ANARIDevice device, void* appMemory, ANARIMemoryDeleter deleter, void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t byteStride1) override;
    void anariNewArray2D(ANARIDevice device, void* appMemory, ANARIMemoryDeleter deleter, void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t byteStride1, uint64_t byteStride2) override;
    void anariNewArray3D(ANARIDevice device, void* appMemory, ANARIMemoryDeleter deleter, void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t numItems3, uint64_t byteStride1, uint64_t byteStride2, uint64_t byteStride3) override;
    void anariMapArray(ANARIDevice device, ANARIArray array) override;
    void anariUnmapArray(ANARIDevice device, ANARIArray array) override;
    void anariNewLight(ANARIDevice device, const char* type) override;
    void anariNewCamera(ANARIDevice device, const char* type) override;
    void anariNewGeometry(ANARIDevice device, const char* type) override;
    void anariNewSpatialField(ANARIDevice device, const char* type) override;
    void anariNewVolume(ANARIDevice device, const char* type) override;
    void anariNewSurface(ANARIDevice device) override;
    void anariNewMaterial(ANARIDevice device, const char* type) override;
    void anariNewSampler(ANARIDevice device, const char* type) override;
    void anariNewGroup(ANARIDevice device) override;
    void anariNewInstance(ANARIDevice device) override;
    void anariNewWorld(ANARIDevice device) override;
    void anariNewObject(ANARIDevice device, const char* objectType, const char* type) override;
    void anariSetParameter(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, const void* mem) override;
    void anariUnsetParameter(ANARIDevice device, ANARIObject object, const char* name) override;
    void anariCommit(ANARIDevice device, ANARIObject object) override;
    void anariRelease(ANARIDevice device, ANARIObject object) override;
    void anariRetain(ANARIDevice device, ANARIObject object) override;
    void anariGetProperty(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType type, void* mem, uint64_t size, ANARIWaitMask mask) override;
    void anariNewFrame(ANARIDevice device) override;
    void anariMapFrame(ANARIDevice device, ANARIFrame frame, const char* channel) override;
    void anariUnmapFrame(ANARIDevice device, ANARIFrame frame, const char* channel) override;
    void anariNewRenderer(ANARIDevice device, const char* type) override;
    void anariRenderFrame(ANARIDevice device, ANARIFrame frame) override;
    void anariFrameReady(ANARIDevice device, ANARIFrame frame, ANARIWaitMask mask) override;
    void anariDiscardFrame(ANARIDevice device, ANARIFrame frame) override;
    void anariReleaseDevice(ANARIDevice device) override;
};

}
}
