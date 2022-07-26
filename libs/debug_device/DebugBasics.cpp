// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DebugBasics.h"
#include <inttypes.h>

namespace anari {
namespace debug_device {

#define DEBUG_FUNCTION(NAME)\
    const char *functionname = #NAME;\
    ANARIObject sourceObject = device;\
    ANARIDataType sourceType = ANARI_DEVICE;

#define DEBUG_FUNCTION_SOURCE(NAME, OBJECT)\
    const char *functionname = #NAME;\
    ANARIObject sourceObject = nullptr;\
    ANARIDataType sourceType = ANARI_OBJECT;\
    auto info = td->getObjectInfo(OBJECT);\
    if(info == nullptr) {\
        td->reportStatus(\
            nullptr,\
            ANARI_OBJECT,\
            ANARI_SEVERITY_ERROR,\
            ANARI_STATUS_INVALID_ARGUMENT,\
            "%s: Unknown object.", functionname);\
    } else if(info->getRefCount()<=0) {\
        td->reportStatus(\
            OBJECT,\
            info->getType(),\
            ANARI_SEVERITY_ERROR,\
            ANARI_STATUS_INVALID_ARGUMENT,\
            "%s: Object (%s) has been released", functionname, info->getName());\
        sourceObject = OBJECT;\
        sourceType = info->getType();\
    } else {\
        sourceObject = OBJECT;\
        sourceType = info->getType();\
    }

#define DEBUG_SOURCE_INFO info
#define DEBUG_SOURCE_OBJECT sourceObject
#define DEBUG_SOURCE_TYPE sourceType
#define DEBUG_FUNCTION_NAME functionname

#define DEBUG_REPORT(SEVERITY, STATUS, ...)\
    td->reportStatus(\
        DEBUG_SOURCE_OBJECT,\
        DEBUG_SOURCE_TYPE,\
        SEVERITY,\
        STATUS, __VA_ARGS__);

DebugBasics::DebugBasics(DebugDevice *td) : td(td) { }
void DebugBasics::anariNewArray1D(ANARIDevice device, const void *appMemory, ANARIMemoryDeleter deleter,
    const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t byteStride1) {
    DEBUG_FUNCTION(anariNewArray1D)
    (void)device;
    (void)numItems1;
    (void)byteStride1;

    if(appMemory == nullptr && deleter != nullptr) {
        DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
            "%s: Managed array created with a non-null deleter",
            DEBUG_FUNCTION_NAME);
    }

    if(deleter == nullptr && userData != nullptr) {
        DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
            "%s: deleter is NULL but userData != NULL",
            DEBUG_FUNCTION_NAME);
    }

    if(isObject(dataType)) {
        ANARIObject *objects = (ANARIObject*)appMemory;
        for(uint64_t i = 0;i<numItems1;++i) {
            if(objects[i] == 0) {
                DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
                    "%s: Null handle in object array at index %d",
                    DEBUG_FUNCTION_NAME, i);

            } else if(auto elementinfo = td->getObjectInfo(objects[i])) {
                if(elementinfo->getType() != dataType) {
                    DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
                        "%s: Type mismatch in object array at index %d. Array is of type %s but object is %s",
                        DEBUG_FUNCTION_NAME, i, toString(dataType),
                        toString(elementinfo->getType()));
                }
                if(elementinfo->getRefCount() <= 0) {
                    DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
                        "%s: Released handle in object array at index %d.",
                        DEBUG_FUNCTION_NAME, i);
                }
            } else {
                DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
                    "%s: Unknown handle in object array at index %d.",
                    DEBUG_FUNCTION_NAME, i);
            }
        }
    }
}
void DebugBasics::anariNewArray2D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter,
    const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2,
    uint64_t byteStride1, uint64_t byteStride2) {
    DEBUG_FUNCTION(anariNewArray2D)
    (void)device;
    (void)dataType;
    (void)numItems1;
    (void)numItems2;
    (void)byteStride1;
    (void)byteStride2;

    if(appMemory == nullptr && deleter != nullptr) {
        DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
            "%s: Managed array created with a non-null deleter",
            DEBUG_FUNCTION_NAME);
    }

    if(deleter == nullptr && userData != nullptr) {
        DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
            "%s: deleter is NULL but userData != NULL",
            DEBUG_FUNCTION_NAME);
    }
}
void DebugBasics::anariNewArray3D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter,
    const void* userData, ANARIDataType dataType, uint64_t numItems1, uint64_t numItems2, uint64_t numItems3,
    uint64_t byteStride1, uint64_t byteStride2, uint64_t byteStride3) {
    DEBUG_FUNCTION(anariNewArray3D)
    (void)device;
    (void)dataType;
    (void)numItems1;
    (void)numItems2;
    (void)numItems3;
    (void)byteStride1;
    (void)byteStride2;
    (void)byteStride3;

    if(appMemory == nullptr && deleter != nullptr) {
        DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
            "%s: Managed array created with a non-null deleter",
            DEBUG_FUNCTION_NAME);
    }

    if(deleter == nullptr && userData != nullptr) {
        DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
            "%s: deleter is NULL but userData != NULL",
            DEBUG_FUNCTION_NAME);
    }
}
void DebugBasics::anariMapArray(ANARIDevice device, ANARIArray array) {
    DEBUG_FUNCTION_SOURCE(anariMapArray, array)
    (void)device;
}
void DebugBasics::anariUnmapArray(ANARIDevice device, ANARIArray array) {
    DEBUG_FUNCTION_SOURCE(anariMapArray, array)
    (void)device;
}
void DebugBasics::anariNewLight(ANARIDevice device, const char* type) {
    (void)device;
    (void)type;
}
void DebugBasics::anariNewCamera(ANARIDevice device, const char* type) {
    (void)device;
    (void)type;
}
void DebugBasics::anariNewGeometry(ANARIDevice device, const char* type) {
    (void)device;
    (void)type;
}
void DebugBasics::anariNewSpatialField(ANARIDevice device, const char* type) {
    (void)device;
    (void)type;
}
void DebugBasics::anariNewVolume(ANARIDevice device, const char* type) {
    (void)device;
    (void)type;
}
void DebugBasics::anariNewSurface(ANARIDevice device) {
    (void)device;
}
void DebugBasics::anariNewMaterial(ANARIDevice device, const char* type) {
    (void)device;
    (void)type;
}
void DebugBasics::anariNewSampler(ANARIDevice device, const char* type) {
    (void)device;
    (void)type;
}
void DebugBasics::anariNewGroup(ANARIDevice device) {
    (void)device;
}
void DebugBasics::anariNewInstance(ANARIDevice device) {
    (void)device;
}
void DebugBasics::anariNewWorld(ANARIDevice device) {
    (void)device;
}
void DebugBasics::anariNewObject(ANARIDevice device, const char* objectType, const char* type) {
    (void)device;
    (void)objectType;
    (void)type;
}
void DebugBasics::anariSetParameter(ANARIDevice device, ANARIObject object, const char* name,
    ANARIDataType dataType, const void *mem) {

    DEBUG_FUNCTION_SOURCE(anariSetParameter, object)
    (void)device;
    (void)name;

    if(isObject(dataType)) {
        ANARIObject wrappedParam = *(ANARIObject*)mem;
        if(auto paraminfo = td->getObjectInfo(wrappedParam)) {
            if(paraminfo->getRefCount() <= 0) {
                DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
                    "%s: Parameter object (%s) has been released.",
                    DEBUG_FUNCTION_NAME, paraminfo->getName());
            }
            if(paraminfo->getUncommittedParameters() > 0) {
                DEBUG_REPORT(ANARI_SEVERITY_WARNING, ANARI_STATUS_NO_ERROR,
                    "%s: Parameter object (%s) has uncommitted parameters.",
                    DEBUG_FUNCTION_NAME, paraminfo->getName());
            }
            if(paraminfo->getType() != dataType) {
                DEBUG_REPORT(ANARI_SEVERITY_WARNING, ANARI_STATUS_NO_ERROR,
                    "%s: Parameter object (%s) is set as %s has type %s.",
                    DEBUG_FUNCTION_NAME, paraminfo->getName(),
                    toString(dataType), toString(paraminfo->getType()));
            }
        } else {
            DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
                "%s: Unknown object in parameter value", DEBUG_FUNCTION_NAME);
        }
    }

}
void DebugBasics::anariUnsetParameter(ANARIDevice device, ANARIObject object, const char* name) {
    DEBUG_FUNCTION_SOURCE(anariUnsetParameter, object)
    (void)device;
    (void)name;
}
void DebugBasics::anariCommitParameters(ANARIDevice device, ANARIObject object) {
    DEBUG_FUNCTION_SOURCE(anariCommitParameters, object)
    (void)device;
    if(DEBUG_SOURCE_INFO->getUncommittedParameters() == 0) {
        DEBUG_REPORT(ANARI_SEVERITY_WARNING, ANARI_STATUS_NO_ERROR,
            "%s: No parameters to be committed on object (%s).", DEBUG_FUNCTION_NAME, DEBUG_SOURCE_INFO->getName());
    }
}
void DebugBasics::anariRelease(ANARIDevice device, ANARIObject object) {
    DEBUG_FUNCTION_SOURCE(anariRelease, object)
    (void)device;
    if(DEBUG_SOURCE_INFO->getRefCount()==1 && DEBUG_SOURCE_INFO->getReferences() == 0) {
        DEBUG_REPORT(ANARI_SEVERITY_WARNING, ANARI_STATUS_NO_ERROR,
            "%s: Releasing unused object (%s).", DEBUG_FUNCTION_NAME, DEBUG_SOURCE_INFO->getName());
    }
}
void DebugBasics::anariRetain(ANARIDevice device, ANARIObject object) {
    DEBUG_FUNCTION_SOURCE(anariRetain, object)
    (void)device;
}
void DebugBasics::anariGetProperty(ANARIDevice device, ANARIObject object, const char* name,
    ANARIDataType type, void* mem, uint64_t size, ANARIWaitMask mask) {
    DEBUG_FUNCTION_SOURCE(anariGetProperty, object)
    (void)device;
    (void)name;
    (void)mem;
    (void)mask;

    if(size < sizeOf(type)) {
        DEBUG_REPORT(ANARI_SEVERITY_ERROR, ANARI_STATUS_INVALID_ARGUMENT,
            "%s: buffer of size %" PRIu64 " is to small for property of type %s.", DEBUG_FUNCTION_NAME, size, toString(type));
    }
}
void DebugBasics::anariNewFrame(ANARIDevice device) {
    (void)device;
}
void DebugBasics::anariMapFrame(ANARIDevice device, ANARIFrame frame, const char* channel) {
    DEBUG_FUNCTION_SOURCE(anariMapFrame, frame)
    (void)device;
    (void)channel;
}
void DebugBasics::anariUnmapFrame(ANARIDevice device, ANARIFrame frame, const char* channel) {
    DEBUG_FUNCTION_SOURCE(anariUnmapFrame, frame)
    (void)device;
    (void)channel;
}
void DebugBasics::anariNewRenderer(ANARIDevice device, const char* type) {
    (void)device;
    (void)type;
}
void DebugBasics::anariRenderFrame(ANARIDevice device, ANARIFrame frame) {
    DEBUG_FUNCTION_SOURCE(anariRenderFrame, frame)
    (void)device;
    if(DEBUG_SOURCE_INFO->getUncommittedParameters() > 0) {
        DEBUG_REPORT(ANARI_SEVERITY_WARNING, ANARI_STATUS_NO_ERROR,
            "%s: object (%s) has uncommitted parameters.", DEBUG_FUNCTION_NAME, DEBUG_SOURCE_INFO->getName());
    }
}
void DebugBasics::anariFrameReady(ANARIDevice device, ANARIFrame frame, ANARIWaitMask mask) {
    DEBUG_FUNCTION_SOURCE(anariFrameReady, frame)
    (void)device;
    (void)mask;
}
void DebugBasics::anariDiscardFrame(ANARIDevice device, ANARIFrame frame) {
    DEBUG_FUNCTION_SOURCE(anariDiscardFrame, frame)
    (void)device;
}
void DebugBasics::anariReleaseDevice(ANARIDevice device) {
    DEBUG_FUNCTION(anariReleaseDevice)
    (void)device;

    for(size_t i = 1;i<td->objects.size();++i) {
        auto &info = td->objects[i];
        if(info->getRefCount() > 0) {
            DEBUG_REPORT(ANARI_SEVERITY_WARNING, ANARI_STATUS_NO_ERROR,
                "%s: Leaked object (%s).", DEBUG_FUNCTION_NAME, info->getName());
        }
    }
    for(size_t i = 1;i<td->objects.size();++i) {
        auto &info = td->objects[i];
        if(info->getReferences() == 0) {
            DEBUG_REPORT(ANARI_SEVERITY_WARNING, ANARI_STATUS_NO_ERROR,
                "%s: Unused object (%s).", DEBUG_FUNCTION_NAME, info->getName());
        }
    }
}

}
}
