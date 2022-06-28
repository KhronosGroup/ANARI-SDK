// This file was generated by $generator from $template

#pragma once

#include <anari/type_utility.h>
#include <anari/anari_cpp/Traits.h>
#include <cstring>
#include <new>

#include "$prefixString.h"

$begin_namespaces

void anariRetainInternal(ANARIDevice, ANARIObject, ANARIObject);
void anariReleaseInternal(ANARIDevice, ANARIObject, ANARIObject);

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
   typename anari::ANARITypeProperties<T>::array_type data;
public:
   ParameterStorage(ANARIDevice device, ANARIObject object, const void *mem) {
      (void)device;
      (void)object;
      std::memcpy(data, mem, sizeof(data));
   }
   ParameterStorageBase* clone(char *mem) override {
      return new(mem) ParameterStorage(nullptr, nullptr, data);
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
class ParameterStorage<T, typename std::enable_if<anari::isObject(T)>::type> : public ParameterStorageBase {
   typename anari::ANARITypeProperties<T>::array_type data;
   ANARIDevice device;
   ANARIObject object;
public:
   ParameterStorage(ANARIDevice device, ANARIObject object, const void *mem) : device(device), object(object) {
      std::memcpy(data, mem, sizeof(ANARIObject));
      anariRetainInternal(device, data[0], object);
   }
   ~ParameterStorage() {
      anariReleaseInternal(device, data[0], object);
   }
   ParameterStorageBase* clone(char *mem) override {
      return new(mem) ParameterStorage(device, object, data);
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
   ParameterStorage(ANARIDevice device, ANARIObject object, const void *mem) {
      (void)device;
      (void)object;
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
         return new(mem) ParameterStorage(nullptr, nullptr, str);
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
   ParameterStorage(ANARIDevice device, ANARIObject object, const void *mem) : value(mem) {
      (void)device;
      (void)object;
   }
   ParameterStorageBase* clone(char *mem) override {
      return new(mem) ParameterStorage(nullptr, nullptr, value);
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
   ParameterStorageBase* operator()(char *target, ANARIDevice device, ANARIObject obj, const void *mem) {
      return new(target) ParameterStorage<T>(device, obj, mem);
   }
};

class ParameterBase {
public:
   virtual bool set(ANARIDevice device, ANARIObject obj, ANARIDataType type, const void *mem) = 0;
   virtual void unset(ANARIDevice, ANARIObject) = 0;
   virtual bool get(ANARIDataType type, void *mem) = 0;
   virtual int getStringEnum() = 0;
   virtual const char* getString() = 0;
   virtual ANARIObject getHandle() = 0;
   virtual ANARIDataType type() = 0;
   virtual operator bool() = 0;
   virtual uint32_t getVersion() const = 0;
};

class EmptyParameter : public ParameterBase {
public:
   bool set(ANARIDevice, ANARIObject, ANARIDataType, const void*) override { return 0; }
   void unset(ANARIDevice, ANARIObject) override { }
   bool get(ANARIDataType, void*) override { return 0; }
   int getStringEnum() override { return -1; }
   const char* getString() override { return nullptr; }
   ANARIObject getHandle() override { return nullptr; }
   ANARIDataType type() override { return ANARI_UNKNOWN; }
   operator bool() override { return false; }
   uint32_t getVersion() const { return 0; }

};

template<int... T>
class Parameter : public ParameterBase {
   static const size_t byte_size = max_reduce<sizeof(ParameterStorageBase), sizeof(ParameterStorage<T>)...>::value;
   char data[byte_size];
   ParameterStorageBase *ptr;
   uint32_t version;
public:
   Parameter() : ptr(new(data) ParameterStorageBase), version(0) {
   }
   Parameter(const Parameter &that) : ptr(that.clone(data)), version(that.version) {
   }
   Parameter& operator=(const Parameter &that) {
      ptr->~ParameterStorageBase();
      ptr = that.ptr->clone(data);
      version = that.version;
      return *this;
   }
   ~Parameter() {
      ptr->~ParameterStorageBase();
   }
   bool set(ANARIDevice device, ANARIObject obj, ANARIDataType type, const void *mem) override {
      if(contains<T...>(type)) {
         ptr->~ParameterStorageBase();
         ptr = anari::anariTypeInvoke<ParameterStorageBase*, ParameterConstructor>(type, data, device, obj, mem);
         version += 1;
         return true;
      } else {
         return false;
      }
   }
   void unset(ANARIDevice, ANARIObject) override {
      ptr->~ParameterStorageBase();
      ptr = new(data) ParameterStorageBase;
      version += 1;
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
   uint32_t getVersion() const {
      return version;
   }
};

$end_namespaces
