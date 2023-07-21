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
    void anariNewArray1D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1) override;
    void anariNewArray2D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2) override;
    void anariNewArray3D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t numItems3) override;
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
    void anariUnsetAllParameters(ANARIDevice device, ANARIObject object) override;

    void anariMapParameterArray1D(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, uint64_t numElements1, uint64_t *elementStride) override;
    void anariMapParameterArray2D(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, uint64_t numElements1, uint64_t numElements2, uint64_t *elementStride) override;
    void anariMapParameterArray3D(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, uint64_t numElements1, uint64_t numElements2, uint64_t numElements3, uint64_t *elementStride) override;
    void anariUnmapParameterArray(ANARIDevice device, ANARIObject object, const char* name) override;

    void anariCommitParameters(ANARIDevice device, ANARIObject object) override;
    void anariRelease(ANARIDevice device, ANARIObject object) override;
    void anariRetain(ANARIDevice device, ANARIObject object) override;
    void anariGetProperty(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType type, void* mem, uint64_t size, ANARIWaitMask mask) override;
    void anariNewFrame(ANARIDevice device) override;
    void anariMapFrame(ANARIDevice device, ANARIFrame frame, const char* channel, uint32_t *width, uint32_t *height, ANARIDataType *pixelType) override;
    void anariUnmapFrame(ANARIDevice device, ANARIFrame frame, const char* channel) override;
    void anariNewRenderer(ANARIDevice device, const char* type) override;
    void anariRenderFrame(ANARIDevice device, ANARIFrame frame) override;
    void anariFrameReady(ANARIDevice device, ANARIFrame frame, ANARIWaitMask mask) override;
    void anariDiscardFrame(ANARIDevice device, ANARIFrame frame) override;
    void anariReleaseDevice(ANARIDevice device) override;
};

}
}
