// This file was generated by $generator from $template

#pragma once

#include <anari/type_utility.h>
#include <anari/anari_cpp/Traits.h>
#include <cstring>
#include <new>

#include "$prefixString.h"

namespace anari {
namespace $namespace {

void anariRetainInternal(ANARIDevice, ANARIObject);
void anariReleaseInternal(ANARIDevice, ANARIObject);

template<size_t T0, size_t... T>
struct max_reduce {
    static const size_t value = T0>max_reduce<T...>::value?T0:max_reduce<T...>::value;
};
template<size_t T0>
struct max_reduce<T0> {
    static const size_t value = T0;
};

static inline bool anyTrue(bool x) {
    return x;
}
template<typename... T>
static bool anyTrue(bool x, bool y, T ...rest) {
    return x || anyTrue(y, rest...);
}

template<int... T>
bool contains(ANARIDataType type) {
    return anyTrue(type == T...);
}

class ParameterStorageBase {
public:
    virtual bool get(ANARIDataType, void *) const { return false; }
    virtual int getStringEnum() { return -1; }
    virtual const char* getString() { return nullptr; }
    virtual ANARIObject getHandle() { return nullptr; }
    virtual ANARIDataType type() const { return ANARI_UNKNOWN; }
    virtual ~ParameterStorageBase() {}
    virtual ParameterStorageBase* clone(char *mem) { return new(mem) ParameterStorageBase; }
};

template<int T, typename Enable = void>
class ParameterStorage : public ParameterStorageBase {
    typename ANARITypeProperties<T>::array_type data;
public:
    ParameterStorage(ANARIDevice device, const void *mem) {
        std::memcpy(data, mem, sizeof(data));
    }
    ParameterStorageBase* clone(char *mem) override {
        return new(mem) ParameterStorage(nullptr, data);
    }
    bool get(ANARIDataType requested, void *mem) const override {
        if(requested == T) {
            std::memcpy(mem, data, sizeof(data));
            return true;
        } else {
            return false;
        }
    }
    ANARIDataType type() const override { return T; }
};

template<int T>
class ParameterStorage<T, typename std::enable_if<isObject(T)>::type> : public ParameterStorageBase {
    typename ANARITypeProperties<T>::array_type data;
    ANARIDevice device;
public:
    ParameterStorage(ANARIDevice device, const void *mem) : device(device) {
        std::memcpy(data, mem, sizeof(ANARIObject));
        anariRetainInternal(device, data[0]);
    }
    ~ParameterStorage() {
        anariReleaseInternal(device, data[0]);
    }
    ParameterStorageBase* clone(char *mem) override {
        return new(mem) ParameterStorage(device, data);
    }
    bool get(ANARIDataType requested, void *mem) const override {
        if(requested == T) {
            std::memcpy(mem, data, sizeof(ANARIObject));
            return true;
        } else {
            return false;
        }
    }
    ANARIObject getHandle() override { return data[0]; }
    ANARIDataType type() const override { return T; }
};

template<>
class ParameterStorage<ANARI_STRING> : public ParameterStorageBase {
    const char *str = nullptr;
    int enum_value = -1;
    ParameterStorage(int e) : str(param_strings[e]), enum_value(e) {
    }
public:
    ParameterStorage(ANARIDevice device, const void *mem) {
        enum_value = parameter_string_hash((const char*)mem);
        if(enum_value >= 0) {
            str = param_strings[enum_value];
        } else {
            const char *in = static_cast<const char*>(mem);
            size_t len = std::strlen(in);
            char *cpy = new char[len+1];
            std::memcpy(cpy, in, len+1);
            str = cpy;
        }
    }
    ~ParameterStorage() {
        if(enum_value < 0) {
            delete[] str;
        }
    }
    ParameterStorageBase* clone(char *mem) override {
        if(enum_value>=0) {
            return new(mem) ParameterStorage(enum_value);
        } else {
            return new(mem) ParameterStorage(nullptr, str);
        }
    }
    bool get(ANARIDataType requested, void *mem) const override {
        if(requested == ANARI_STRING) {
            *static_cast<const char**>(mem) = str;
            return true;
        } else {
            return false;
        }
    }
    int getStringEnum() override { return enum_value; }
    const char* getString() override { return str; }
    ANARIDataType type() const override { return ANARI_STRING; }
};

template<>
class ParameterStorage<ANARI_VOID_POINTER> : public ParameterStorageBase {
    const void *value;
public:
    ParameterStorage(ANARIDevice device, const void *mem) : value(mem) {
    }
    ParameterStorageBase* clone(char *mem) override {
        return new(mem) ParameterStorage(nullptr, value);
    }
    bool get(ANARIDataType requested, void *mem) const override {
        if(requested == ANARI_VOID_POINTER) {
            *static_cast<const void**>(mem) = value;
            return true;
        } else {
            return false;
        }
    }
    ANARIDataType type() const override { return ANARI_VOID_POINTER; }
};

template<int T>
struct ParameterConstructor {
    ParameterStorageBase* operator()(char *target, ANARIDevice device, const void *mem) {
        return new(target) ParameterStorage<T>(device, mem);
    }
};

class ParameterBase {
public:
    virtual bool set(ANARIDevice device, ANARIDataType type, const void *mem) = 0;
    virtual void unset() = 0;
    virtual bool get(ANARIDataType type, void *mem) = 0;
    virtual int getStringEnum() = 0;
    virtual const char* getString() = 0;
    virtual ANARIObject getHandle() = 0;
    virtual ANARIDataType type() = 0;
    virtual operator bool() = 0;
};

class EmptyParameter : public ParameterBase {
public:
    virtual bool set(ANARIDevice device, ANARIDataType type, const void *mem) { return 0; }
    virtual void unset() { }
    virtual bool get(ANARIDataType type, void *mem) { return 0; }
    virtual int getStringEnum() { return -1; }
    virtual const char* getString() { return nullptr; }
    virtual ANARIObject getHandle() { return nullptr; }
    virtual ANARIDataType type() { return ANARI_UNKNOWN; }
    virtual operator bool() { return false; }
};

template<int... T>
class Parameter : public ParameterBase {
    static const size_t byte_size = max_reduce<sizeof(ParameterStorageBase), sizeof(ParameterStorage<T>)...>::value;
    char data[byte_size];
    ParameterStorageBase *ptr;
public:
    Parameter() : ptr(new(data) ParameterStorageBase) {
    }
    Parameter(const Parameter &that) : ptr(that.clone(data)) {
    }
    Parameter& operator=(const Parameter &that) {
        ptr->~ParameterStorageBase();
        ptr = that.ptr->clone(data);
        return *this;
    }
    ~Parameter() {
        ptr->~ParameterStorageBase();
    }
    bool set(ANARIDevice device, ANARIDataType type, const void *mem) override {
        if(contains<T...>(type)) {
            ptr->~ParameterStorageBase();
            ptr = anariTypeInvoke<ParameterStorageBase*, ParameterConstructor>(type, data, device, mem);
            return true;
        } else {
            return false;
        }
    }
    void unset() override {
        ptr->~ParameterStorageBase();
        ptr = new(data) ParameterStorageBase;
    }
    bool get(ANARIDataType type, void *mem) override {
        return ptr->get(type, mem);
    }
    int getStringEnum() override {
        return ptr->getStringEnum();
    }
    const char* getString() override {
        return ptr->getString();
    }
    ANARIObject getHandle() override {
        return ptr->getHandle();
    }
    ANARIDataType type() override {
        return ptr->type();
    }
    operator bool() override {
        return ptr->type() != ANARI_UNKNOWN;
    }
};

}
}
