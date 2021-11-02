#pragma once

#include "anari_type_properties.h"

namespace {
template<class T, typename std::enable_if<!isObject(anari::ANARITypeFor<T>::value), int>::type = 0>
const T& translate_if_object(const T &val, DebugDevice *dd) {
    (void)dd;
    return val;
}

template<class T, typename std::enable_if<isObject(anari::ANARITypeFor<T>::value), int>::type = 0>
const std::string& translate_if_object(const T &val, DebugDevice *dd) {
    return dd->getObjectName(val);
}


struct AnariPrintArrayDefinition {
    template<int type>
    static std::ostream& invoke(std::ostream &out, uint64_t N, const void *mem0, const char* name, DebugDevice *dd) {
        using props = ANARITypeProperties<type>;
        out << props::type_name << ' ' << name << "[" << N << "]";
        if(props::components > 1) {
            out << "[" << props::components << "]";
        }
        out << " = {";

        const typename props::base_type *mem = static_cast<const typename props::base_type*>(mem0);
        for(uint64_t i = 0;i<N;++i) {
            if(props::components>1) {
                out << "{";
            }

            out << translate_if_object(mem[i*props::components + 0], dd);

            for(int j = 1;j<props::components;++j) {
                out << ", " << translate_if_object(mem[i*props::components + j], dd);
            }

            if(props::components>1) {
                out << "}";
            }
            if(i+1<N) {
                out << ", ";
            }
        }
        out << "};\n";
        return out;
    }
};

struct AnariPrintDefinition {
    template<int type>
    static std::ostream& invoke(std::ostream &out, const void *mem0, const char* name, int index, DebugDevice *dd) {
        using props = ANARITypeProperties<type>;
        out << props::type_name << ' ' << name << index << "[" << props::components << "]";
        out << " = {";

        const typename props::base_type *mem = static_cast<const typename props::base_type*>(mem0);

        out  << translate_if_object(mem[0], dd);

        for(int j = 1;j<props::components;++j) {
            out <<", " << translate_if_object(mem[j], dd);
        }

        out << "};\n";

        return out;
    }
};

struct AnariEnumEnum {
    template<int type>
    static std::ostream& invoke(std::ostream &out) {
        return out << ANARITypeProperties<type>::enum_name;
    }
};
}

static inline std::ostream& anari_type_array_definition(std::ostream &out, ANARIDataType type, uint64_t N, const void *mem0, const char* name, DebugDevice *dd) {
    return anari_type_invoke<std::ostream&, AnariPrintArrayDefinition>(type, out, N, mem0, name, dd);
}

static inline std::ostream& anari_type_definition(std::ostream &out, ANARIDataType type, const void *mem0, const char* name, int index, DebugDevice *dd) {
    return anari_type_invoke<std::ostream&, AnariPrintDefinition>(type, out, mem0, name, index, dd);
}

static inline const char* anari_type_enumname(ANARIDataType type) {
    return anari_type_invoke<const char*, AnariEnumName>(type);
}

/*
#define PRINT_ENUM_CASE(X) case X: return out << #X;

static inline std::ostream& operator<<(std::ostream &out, ANARIFrameFormat format) {
    switch(format) {
        PRINT_ENUM_CASE(ANARI_FB_NONE)
        PRINT_ENUM_CASE(ANARI_FB_RGBA8)
        PRINT_ENUM_CASE(ANARI_FB_SRGBA)
        PRINT_ENUM_CASE(ANARI_FB_RGBA32F)
        default:  return out << "(ANARIFrameFormat)" << uint32_t(format);
    }
}

static inline std::ostream& operator<<(std::ostream &out, ANARISyncEvent event) {
    switch(event) {
        PRINT_ENUM_CASE(ANARI_NONE_FINISHED)
        PRINT_ENUM_CASE(ANARI_WORLD_RENDERED)
        PRINT_ENUM_CASE(ANARI_WORLD_COMMITTED)
        PRINT_ENUM_CASE(ANARI_FRAME_FINISHED)
        PRINT_ENUM_CASE(ANARI_TASK_FINISHED)
        default:  return out << "(ANARISyncEvent)" << uint32_t(event);
    }
}
*/