//
// MaterialXCpp value type — pxr-independent, std::variant-based.
//
#ifndef MXCPP_VALUE_H
#define MXCPP_VALUE_H

#include "mathTypes.h"
#include "surfaceClosure.h"

#include <renderer/integrator/medium.h>
#include <renderer/materials/MaterialXCpp/materials/closureTree.h>

#include <string>
#include <variant>

namespace mxcpp {

struct UniformEdf {
    Vec3f emittance = Vec3f(1.0f);
};

struct BsdfClosure {
    Bsdf::ClosureTree tree;
    bool hasInteriorMedium = false;
    MediumProperties interiorMedium;
};

struct VdfClosure {
    MediumProperties medium;
};

using Value = std::variant<
    std::monostate,
    float,
    int,
    bool,
    Vec2f,
    Vec3f,
    Vec4f,
    Mat3f,
    Mat4f,
    UniformEdf,
    BsdfClosure,
    VdfClosure,
    SurfaceClosure,
    std::string>;

inline bool ValueIsEmpty(const Value& v) {
    return std::holds_alternative<std::monostate>(v);
}

template<typename T>
inline bool ValueHolds(const Value& v) {
    return std::holds_alternative<T>(v);
}

template<typename T>
inline const T& ValueGet(const Value& v) {
    return std::get<T>(v);
}

}  // namespace mxcpp

#endif  // MXCPP_VALUE_H
