#pragma once

#include "anari/anari.h"
#include "anari/anari_cpp/Traits.h"
#include "anari_type_properties.h"

#include <ostream>
#include <cstring>
#include <utility>
#include <stdint.h>

namespace {
struct AnariSizeof {
    template<int type>
    static size_t invoke() {
        return sizeof(typename ANARITypeProperties<type>::array_type);
    }
};

struct AnariBaseType {
    template<int type>
    static ANARIDataType invoke() {
        return anari::ANARITypeFor<typename ANARITypeProperties<type>::base_type>::value;
    }
};

struct AnariComponents {
    template<int type>
    static int invoke() {
        return ANARITypeProperties<type>::components;
    }
};

struct AnariTypeName {
    template<int type>
    static const char* invoke() {
        return ANARITypeProperties<type>::type_name;
    }
};

struct AnariVarName {
    template<int type>
    static const char* invoke() {
        return ANARITypeProperties<type>::var_name;
    }
};

struct AnariEnumName {
    template<int type>
    static const char* invoke() {
        return ANARITypeProperties<type>::enum_name;
    }
};

struct AnariPrintEnum {
    template<int type>
    static std::ostream& invoke(std::ostream &out) {
        return out << ANARITypeProperties<type>::enum_name;
    }
};
}

inline size_t anari_type_size(int type) {
    return anari_type_invoke<size_t, AnariSizeof>(type);
}

inline ANARIDataType anari_type_base(int type) {
    return anari_type_invoke<ANARIDataType, AnariBaseType>(type);
}

inline int anari_type_components(int type) {
    return anari_type_invoke<int, AnariComponents>(type);
}

inline const char* anari_type_name(int type) {
    return anari_type_invoke<const char*, AnariTypeName>(type);
}

inline const char* anari_enum_name(int type) {
    return anari_type_invoke<const char*, AnariEnumName>(type);
}

inline const char* anari_type_varname(int type) {
    return anari_type_invoke<const char*, AnariVarName>(type);
}


class AnariVariantImplBase {
public:
    virtual void print(std::ostream &out) const = 0;
    virtual ANARIDataType getType() const = 0;
    virtual void getMem(void *mem) const = 0;
    virtual void setMem(const void *mem) = 0;
    virtual ~AnariVariantImplBase() = default;
};

template<int type, typename base_type = typename ANARITypeProperties<type>::base_type, int N = ANARITypeProperties<type>::components>
class AnariVariantImpl : public AnariVariantImplBase {
    base_type data[N];
public:
    AnariVariantImpl(const void *mem) {
        std::memcpy(data, mem, sizeof(data));
    }

    ANARIDataType getType() const { return type; }
    void getMem(void *mem) const { std::memcpy(mem, data, sizeof(data)); }
    void setMem(const void *mem) { std::memcpy(data, mem, sizeof(data)); }

    void print(std::ostream &out) const {
        out << data[0];
        for(int i = 1;i<N;++i) {
            out << ", " << data[i];
        }
    }
    ~AnariVariantImpl() = default;
};

struct ConstructAnariVariant {
    template<int type>
    static AnariVariantImplBase* invoke(void *mem, const void *value) {
        return new(mem) AnariVariantImpl<type>(value);
    }
};

class AnariVariant {
    char mem[sizeof(AnariVariantImpl<ANARI_FLOAT32_MAT3x4>)];
    AnariVariantImplBase *impl = nullptr;
public:
    void unset() {
        if(impl != nullptr) {
            impl->~AnariVariantImplBase();
            impl = nullptr;
        }
    }
    void set(int type, const void *value) {
        if(impl != nullptr && impl->getType() == type) {
            impl->setMem(value);
        } else {
            unset();
            impl = anari_type_invoke<AnariVariantImplBase*, ConstructAnariVariant>(type, mem, value);
        }
    }
    bool get(int type, void *value) const {
        if(impl != nullptr && impl->getType() == type) {
            impl->getMem(value);
            return true;
        } else {
            return false;
        }
    }
    void print(std::ostream &out) const {
        if(impl) {
            impl->print(out);
        }
    }
    ~AnariVariant() {
        //unset();
    }

};
