// This file was generated by $generator from $template

#include "$prefixArrayObjects.h"
#include "anari/type_utility.h"

#include <cstdlib>
#include <cstring>
#include <algorithm>

static void managed_deleter(void*, void *appMemory) {
    std::free(appMemory);
}

$begin_namespaces

Object<Array1D>::Object(ANARIDevice d, ANARIObject handle, void* appMemory,
    ANARIMemoryDeleter deleter, void* userdata, ANARIDataType elementType,
    uint64_t numItems1, uint64_t byteStride1)
: ArrayObjectBase(d, handle), staging(d), current(d), appMemory(appMemory), deleter(deleter), userdata(userdata),
elementType(elementType), numItems1(numItems1), byteStride1(byteStride1)
{
    if(this->byteStride1 == 0) {
        this->byteStride1 = anari::sizeOf(elementType);
    }
    if(appMemory == nullptr) {
        this->appMemory = std::calloc(numItems1, anari::sizeOf(elementType));
        this->deleter = managed_deleter;
    }
    if(anari::isObject(elementType)) {
        objectArray.resize(numItems1);
        char *basePtr = static_cast<char*>(this->appMemory);
        for(uint64_t i = 0;i<numItems1;++i) {
            ANARIObject op = *reinterpret_cast<ANARIObject*>(basePtr+i*this->byteStride1);
            objectArray[i] = op;
            anariRetainInternal(device, objectArray[i]);
        }
    }
}

bool Object<Array1D>::set(const char *paramname, ANARIDataType type, const void *mem) {
    return staging.set(paramname, type, mem);
}
void Object<Array1D>::unset(const char *paramname) {
    staging.unset(paramname);
}
void Object<Array1D>::commit() {
    current = staging;
}
int Object<Array1D>::getProperty(
    const char *propname, ANARIDataType type,
    void *mem, uint64_t size, ANARIWaitMask mask)
{
    return 0;
}
void* Object<Array1D>::map() {
    return appMemory;
}
void Object<Array1D>::unmap() {
    if(anari::isObject(elementType)) {
        char *basePtr = static_cast<char*>(appMemory);
        for(uint64_t i = 0;i<numItems1;++i) {
            ANARIObject op = *reinterpret_cast<ANARIObject*>(basePtr+i*byteStride1);
            anariReleaseInternal(device, objectArray[i]);
            objectArray[i] = op;
            anariRetainInternal(device, objectArray[i]);
        }
    }
}
void Object<Array1D>::releasePublic() {
    // internalize data if necessary
}
Object<Array1D>::~Object() {
    for(auto handle : objectArray) {
        anariReleaseInternal(device, handle);
    }
    if(deleter) {
        deleter(userdata, appMemory);
    }
}


Object<Array2D>::Object(ANARIDevice d, ANARIObject handle, void* appMemory,
    ANARIMemoryDeleter deleter, void* userdata, ANARIDataType elementType,
    uint64_t numItems1, uint64_t numItems2,
    uint64_t byteStride1, uint64_t byteStride2)
: ArrayObjectBase(d, handle), staging(d), current(d), appMemory(appMemory), deleter(deleter), userdata(userdata),
elementType(elementType), numItems1(numItems1), numItems2(numItems2),
byteStride1(byteStride1), byteStride2(byteStride2)
{
    if(this->byteStride1 == 0) {
        this->byteStride1 = anari::sizeOf(elementType);
    }
    if(this->byteStride2 == 0) {
        this->byteStride2 = this->byteStride1*numItems1;
    }

    if(appMemory == nullptr) {
        size_t byte_size = anari::sizeOf(elementType)*numItems1*numItems2;
        appMemory = std::malloc(byte_size);
        deleter = managed_deleter;
    }
}

bool Object<Array2D>::set(const char *paramname, ANARIDataType type, const void *mem) {
    return staging.set(paramname, type, mem);
}
void Object<Array2D>::unset(const char *paramname) {
    staging.unset(paramname);
}
void Object<Array2D>::commit() {
    current = staging;
}
int Object<Array2D>::getProperty(
    const char *propname, ANARIDataType type,
    void *mem, uint64_t size, ANARIWaitMask mask)
{
    return 0;
}
void* Object<Array2D>::map() {
    return appMemory;
}
void Object<Array2D>::unmap() {

}
void Object<Array2D>::releasePublic() {
    // internalize data if necessary
}
Object<Array2D>::~Object() {
    if(deleter) {
        deleter(userdata, appMemory);
    }
}


Object<Array3D>::Object(ANARIDevice d, ANARIObject handle, void* appMemory,
    ANARIMemoryDeleter deleter, void* userdata, ANARIDataType elementType,
    uint64_t numItems1, uint64_t numItems2, uint64_t numItems3,
    uint64_t byteStride1, uint64_t byteStride2, uint64_t byteStride3)
: ArrayObjectBase(d, handle), staging(d), current(d), appMemory(appMemory), deleter(deleter), userdata(userdata),
elementType(elementType), numItems1(numItems1), numItems2(numItems2), numItems3(numItems3),
byteStride1(byteStride1), byteStride2(byteStride2), byteStride3(byteStride3)
{
    if(this->byteStride1 == 0) {
        this->byteStride1 = anari::sizeOf(elementType);
    }
    if(this->byteStride2 == 0) {
        this->byteStride2 = this->byteStride1*numItems1;
    }
    if(this->byteStride3 == 0) {
        this->byteStride3 = this->byteStride2*numItems2;
    }

    if(appMemory == nullptr) {
        size_t byte_size = anari::sizeOf(elementType)*numItems1*numItems2*numItems3;
        appMemory = std::malloc(byte_size);
        deleter = managed_deleter;
    }
}

bool Object<Array3D>::set(const char *paramname, ANARIDataType type, const void *mem) {
    return staging.set(paramname, type, mem);
}
void Object<Array3D>::unset(const char *paramname) {
    staging.unset(paramname);
}
void Object<Array3D>::commit() {
    current = staging;
}
int Object<Array3D>::getProperty(
    const char *propname, ANARIDataType type,
    void *mem, uint64_t size, ANARIWaitMask mask)
{
    return 0;
}
void* Object<Array3D>::map() {
    return appMemory;
}
void Object<Array3D>::unmap() {

}
void Object<Array3D>::releasePublic() {
    // internalize data if necessary
}
Object<Array3D>::~Object() {
    if(deleter) {
        deleter(userdata, appMemory);
    }
}

$end_namespaces
