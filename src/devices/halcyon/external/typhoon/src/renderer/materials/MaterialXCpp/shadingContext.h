//
// MaterialXCpp shading context — pxr-independent runtime state.
//
#ifndef MXCPP_SHADING_CONTEXT_H
#define MXCPP_SHADING_CONTEXT_H

#include "mathTypes.h"
#include "textureSystem.h"
#include "value.h"

#include <string>
#include <vector>

namespace mxcpp {

/// Geometric context provided to node evaluation at each shading point.
struct ShadingContext
{
    enum class TransformSpaceType {
        Point,
        Vector,
        Normal
    };

    using TransformSpaceFn = bool(*)(
        const void* userData,
        const std::string& fromSpace,
        const std::string& toSpace,
        TransformSpaceType type,
        const Vec3f& in,
        Vec3f* out);

    // Object-space position, matching MaterialX's default Pobject behavior.
    Vec3f position = Vec3f(0.0f);
    // World-space shading frame, matching MaterialX defaults such as Nworld.
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    Vec3f tangent = Vec3f(1.0f, 0.0f, 0.0f);
    Vec3f bitangent = Vec3f(0.0f, 1.0f, 0.0f);
    Vec3f viewPosition = Vec3f(0.0f);
    Vec2f texcoord = Vec2f(0.0f);
    Vec3f displayColor = Vec3f(0.8f);
    float displayOpacity = 1.0f;
    float frame = 0.0f;
    float time = 0.0f;
    int faceId = 0;
    float baryU = 0.0f;
    float baryV = 0.0f;

    // World-space surface derivatives in the active parameterization basis.
    // With "st", these are dP/ds and dP/dt; otherwise they fall back to the
    // coarse triangle's local barycentric-edge basis.
    Vec3f dPdu = Vec3f(0.0f);
    Vec3f dPdv = Vec3f(0.0f);

    // World-space screen-space position derivatives (from ray differentials).
    Vec3f dPdx = Vec3f(0.0f);
    Vec3f dPdy = Vec3f(0.0f);

    // Object-space position derivatives used when reevaluating upstream nodes
    // that depend on MaterialX's default Pobject position.
    Vec3f dPositiondu = Vec3f(0.0f);
    Vec3f dPositiondv = Vec3f(0.0f);
    Vec3f dPositiondx = Vec3f(0.0f);
    Vec3f dPositiondy = Vec3f(0.0f);

    // Screen-space coefficients for the active derivative basis above.
    // These are true texture derivatives only when dPdu/dPdv represent st.
    float dudx = 0.0f, dvdx = 0.0f;
    float dudy = 0.0f, dvdy = 0.0f;

    // Additional MaterialX blur amount for texture lookups in UV space.
    // This is subtree-local state set by ND_blur_* and consumed by image
    // nodes when constructing Texture2DRequest values.
    Vec2f textureBlur = Vec2f(0.0f);

    // Named-space transform support for geometric and transform* nodes.
    // The callback can handle arbitrary renderer-defined spaces; when it is
    // absent or declines a transform, mxcpp falls back to built-in object/world
    // transforms using the matrices below. It must report recoverable failures
    // by returning false rather than throwing.
    TransformSpaceFn transformSpace = nullptr;
    const void* transformUserData = nullptr;
    std::string workingSpace = "world";
    Mat4f objectToWorldMatrix;
    Mat4f worldToObjectMatrix;
    bool hasObjectToWorldTransform = false;
    bool hasWorldToObjectTransform = false;

    // Per-sample varying property lookup (geompropvalue). Handles are indexed
    // against the material name table used to compile the active graph.
    // Returns std::monostate (empty Value) on failure and must not throw for
    // recoverable lookup failures.
    using GeomPropFn = Value(*)(const void* userData, int geomPropHandle);
    GeomPropFn geomPropLookup = nullptr;
    const void* geomPropUserData = nullptr;

    // Per-mesh uniform properties indexed against the material name table
    // used to compile the active graph. Lifetime is managed by the caller.
    const std::vector<Value>* uniformProps = nullptr;

    // Optional color transform callback for MaterialX colortransform nodes.
    // The callback receives canonical lower-case color-space names and RGB
    // triplets only. color4 alpha passthrough is handled in mxcpp. Recoverable
    // transform failures must return false rather than throw.
    using ColorTransformFn = bool(*)(
        const void* userData,
        const std::string& sourceColorSpace,
        const std::string& targetColorSpace,
        const Vec3f& in,
        Vec3f* out);
    ColorTransformFn colorTransform = nullptr;
    const void* colorTransformUserData = nullptr;
    // Global renderer data mode overrides authored color-transform nodes.
    bool bypassColorTransforms = false;

    // CIE Y coefficients for renderer-dependent lobe weighting and clamping.
    Vec3f luminanceCoefficients =
        Vec3f(0.212639005871510f,
              0.715168678767756f,
              0.072192315360734f);

    // Texture lookup backend owned by the embedding renderer. The pointer is
    // non-owning and may be null, in which case texture nodes fall back to
    // their authored default values.
    const TextureSystem* textureSystem = nullptr;
};

}  // namespace mxcpp

#endif  // MXCPP_SHADING_CONTEXT_H
