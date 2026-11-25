//
// MaterialXCpp BSDF closure tree representation.
//
#ifndef MXCPP_MATERIALS_CLOSURE_TREE_H
#define MXCPP_MATERIALS_CLOSURE_TREE_H

#include <renderer/materials/MaterialXCpp/mathTypes.h>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace mxcpp {
namespace Bsdf {

using NodeId = std::uint32_t;
constexpr NodeId InvalidNodeId = std::numeric_limits<NodeId>::max();

enum class ScatterMode
{
    Reflection,
    Transmission,
    ReflectionTransmission
};

enum class SheenMode
{
    ContyKulla,
    Zeltner
};

enum class UnsupportedNodeKind
{
    Subsurface,
    ChiangHair
};

struct OrenNayarDiffuseData
{
    float weight = 1.0f;
    Vec3f color = Vec3f(0.18f);
    float roughness = 0.0f;
    bool energyCompensation = false;
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    bool hasShadingNormal = false;
};

struct BurleyDiffuseData
{
    float weight = 1.0f;
    Vec3f color = Vec3f(0.18f);
    float roughness = 0.0f;
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    bool hasShadingNormal = false;
};

struct TranslucentData
{
    float weight = 1.0f;
    Vec3f color = Vec3f(1.0f);
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    bool hasShadingNormal = false;
};

struct SubsurfaceData
{
    float weight = 1.0f;
    Vec3f color = Vec3f(1.0f);
    Vec3f radius = Vec3f(1.0f);
    float anisotropy = 0.0f;
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    bool hasShadingNormal = false;
};

struct DielectricData
{
    float weight = 1.0f;
    Vec3f tint = Vec3f(1.0f);
    float ior = 1.5f;
    float dispersionAbbe = 0.0f;
    Vec2f roughness = Vec2f(0.05f, 0.05f);
    bool retroreflective = false;
    float thinFilmWeight = 1.0f;
    float thinFilmThickness = 0.0f;
    float thinFilmIor = 1.5f;
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    bool hasShadingNormal = false;
    Vec3f tangent = Vec3f(1.0f, 0.0f, 0.0f);
    ScatterMode scatterMode = ScatterMode::Reflection;
};

struct DielectricInterfaceData
{
    float reflectionWeight = 1.0f;
    Vec3f reflectionTint = Vec3f(1.0f);
    float transmissionWeight = 1.0f;
    Vec3f transmissionTint = Vec3f(1.0f);
    float ior = 1.5f;
    float dispersionAbbe = 0.0f;
    Vec2f roughness = Vec2f(0.05f, 0.05f);
    float thinFilmWeight = 1.0f;
    float thinFilmThickness = 0.0f;
    float thinFilmIor = 1.5f;
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    bool hasShadingNormal = false;
    Vec3f tangent = Vec3f(1.0f, 0.0f, 0.0f);
    bool thinWalled = false;
    // Restore microfacet energy missing from the combined reflection and
    // refraction lobes. OpenPBR and metalness-workflow UsdPreviewSurface opt
    // into this policy; other interface users remain uncompensated by default.
    bool compensateCoupledDielectric = false;
};

struct ConductorData
{
    float weight = 1.0f;
    Vec3f ior = Vec3f(0.183f, 0.421f, 1.373f);
    Vec3f extinction = Vec3f(3.424f, 2.346f, 1.770f);
    Vec2f roughness = Vec2f(0.05f, 0.05f);
    bool retroreflective = false;
    float thinFilmWeight = 1.0f;
    float thinFilmThickness = 0.0f;
    float thinFilmIor = 1.5f;
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    bool hasShadingNormal = false;
    Vec3f tangent = Vec3f(1.0f, 0.0f, 0.0f);
};

struct GeneralizedSchlickData
{
    float weight = 1.0f;
    Vec3f color0 = Vec3f(1.0f);
    Vec3f color82 = Vec3f(1.0f);
    Vec3f color90 = Vec3f(1.0f);
    float exponent = 5.0f;
    Vec2f roughness = Vec2f(0.05f, 0.05f);
    bool retroreflective = false;
    float thinFilmWeight = 1.0f;
    float thinFilmThickness = 0.0f;
    float thinFilmIor = 1.5f;
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    bool hasShadingNormal = false;
    Vec3f tangent = Vec3f(1.0f, 0.0f, 0.0f);
    ScatterMode scatterMode = ScatterMode::Reflection;
};

struct SheenData
{
    float weight = 1.0f;
    Vec3f color = Vec3f(1.0f);
    float roughness = 0.3f;
    SheenMode mode = SheenMode::ContyKulla;
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    bool hasShadingNormal = false;
};

struct AdobeOpenPbrData
{
    float baseWeight = 1.0f;
    Vec3f baseColor = Vec3f(0.8f);
    float baseDiffuseRoughness = 0.0f;
    float baseMetalness = 0.0f;

    float subsurfaceWeight = 0.0f;
    Vec3f subsurfaceColor = Vec3f(0.8f);
    float subsurfaceRadius = 1.0f;
    Vec3f subsurfaceRadiusScale = Vec3f(1.0f, 0.5f, 0.25f);
    float subsurfaceScatterAnisotropy = 0.0f;

    float specularWeight = 1.0f;
    Vec3f specularColor = Vec3f(1.0f);
    float specularRoughness = 0.3f;
    float specularRoughnessAnisotropy = 0.0f;
    float specularIor = 1.5f;

    float coatWeight = 0.0f;
    Vec3f coatColor = Vec3f(1.0f);
    float coatRoughness = 0.0f;
    float coatRoughnessAnisotropy = 0.0f;
    float coatIor = 1.6f;
    float coatDarkening = 1.0f;

    float fuzzWeight = 0.0f;
    Vec3f fuzzColor = Vec3f(1.0f);
    float fuzzRoughness = 0.5f;

    float transmissionWeight = 0.0f;
    Vec3f transmissionColor = Vec3f(1.0f);
    float transmissionDepth = 0.0f;
    Vec3f transmissionScatter = Vec3f(0.0f);
    float transmissionScatterAnisotropy = 0.0f;
    float transmissionDispersionScale = 0.0f;
    float transmissionDispersionAbbeNumber = 20.0f;

    float thinFilmWeight = 0.0f;
    float thinFilmThickness = 0.5f;
    float thinFilmIor = 1.4f;

    float emissionLuminance = 0.0f;
    Vec3f emissionColor = Vec3f(1.0f);

    float geometryOpacity = 1.0f;
    bool geometryThinWalled = false;
    Vec3f geometryNormal = Vec3f(0.0f, 0.0f, 1.0f);
    Vec3f geometryTangent = Vec3f(1.0f, 0.0f, 0.0f);
    Vec3f geometryCoatNormal = Vec3f(0.0f, 0.0f, 1.0f);
    Vec3f geometryCoatTangent = Vec3f(1.0f, 0.0f, 0.0f);

    // World-space normal prepared once after graph-normal resolution. Adobe's
    // whole-model backend has one geometry basis, so all eval/sample/PDF calls
    // must consume the same reflection-safe basis.
    Vec3f normal = Vec3f(0.0f, 0.0f, 1.0f);
    bool hasShadingNormal = false;
};

struct UnsupportedData
{
    UnsupportedNodeKind kind = UnsupportedNodeKind::Subsurface;
};

struct MixData
{
    NodeId fg = InvalidNodeId;
    NodeId bg = InvalidNodeId;
    float mix = 0.0f;
};

struct LayerData
{
    NodeId top = InvalidNodeId;
    NodeId base = InvalidNodeId;
};

struct AddData
{
    NodeId in1 = InvalidNodeId;
    NodeId in2 = InvalidNodeId;
};

struct MultiplyData
{
    NodeId input = InvalidNodeId;
    Vec3f weight = Vec3f(1.0f);
};

using NodeData = std::variant<
    OrenNayarDiffuseData,
    BurleyDiffuseData,
    TranslucentData,
    SubsurfaceData,
    DielectricData,
    DielectricInterfaceData,
    ConductorData,
    GeneralizedSchlickData,
    SheenData,
    AdobeOpenPbrData,
    UnsupportedData,
    MixData,
    LayerData,
    AddData,
    MultiplyData>;

struct Node
{
    NodeData data;
};

struct ClosureTree
{
    std::vector<Node> nodes;
    NodeId root = InvalidNodeId;
    Vec3f luminanceCoefficients =
        Vec3f(0.212639005871510f,
              0.715168678767756f,
              0.072192315360734f);

    void Clear()
    {
        nodes.clear();
        root = InvalidNodeId;
    }

    bool Empty() const
    {
        return !IsValid(root);
    }

    bool IsValid(NodeId id) const
    {
        return id != InvalidNodeId && id < nodes.size();
    }

    template <class T>
    NodeId Add(T data)
    {
        const NodeId nodeId = static_cast<NodeId>(nodes.size());
        nodes.push_back(Node{NodeData{std::move(data)}});
        return nodeId;
    }

    NodeId Add(NodeData data)
    {
        const NodeId nodeId = static_cast<NodeId>(nodes.size());
        nodes.push_back(Node{std::move(data)});
        return nodeId;
    }

    const Node* Get(NodeId id) const
    {
        return IsValid(id) ? &nodes[id] : nullptr;
    }

};

}  // namespace Bsdf
}  // namespace mxcpp

#endif  // MXCPP_MATERIALS_CLOSURE_TREE_H
