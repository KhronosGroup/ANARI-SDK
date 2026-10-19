//
// MaterialXCpp value helpers — default value construction utilities.
//
#ifndef MXCPP_NODES_VALUE_HELPER_H
#define MXCPP_NODES_VALUE_HELPER_H

#include <renderer/materials/MaterialXCpp/value.h>

#include <string>

namespace mxcpp {

/// Zero value helpers for template-based node implementations.
template<typename T> inline T Zero();
template<> inline float Zero<float>() { return 0.0f; }
template<> inline int Zero<int>() { return 0; }
template<> inline bool Zero<bool>() { return false; }
template<> inline std::string Zero<std::string>() { return std::string(); }
template<> inline Vec2f Zero<Vec2f>() { return Vec2f(0.0f); }
template<> inline Vec3f Zero<Vec3f>() { return Vec3f(0.0f); }
template<> inline Vec4f Zero<Vec4f>() { return Vec4f(0.0f); }
template<> inline Mat3f Zero<Mat3f>() { return Mat3f(0.0f); }
template<> inline Mat4f Zero<Mat4f>() { return Mat4f(0.0f); }

/// One value helpers for template-based node implementations.
template<typename T> inline T One();
template<> inline float One<float>() { return 1.0f; }
template<> inline int One<int>() { return 1; }
template<> inline bool One<bool>() { return true; }
template<> inline Vec2f One<Vec2f>() { return Vec2f(1.0f); }
template<> inline Vec3f One<Vec3f>() { return Vec3f(1.0f); }
template<> inline Vec4f One<Vec4f>() { return Vec4f(1.0f); }
template<> inline Mat3f One<Mat3f>() { return Mat3f(); }
template<> inline Mat4f One<Mat4f>() { return Mat4f(); }

}  // namespace mxcpp

#endif  // MXCPP_NODES_VALUE_HELPER_H
