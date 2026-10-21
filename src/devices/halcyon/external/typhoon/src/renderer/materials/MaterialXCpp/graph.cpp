//
// MaterialXCpp evaluation graph — pxr-independent.
//
#include "graph.h"
#include "paramMap.h"
#include "surfaceShaderUtils.h"

#include <renderer/materials/MaterialXCpp/nodes/helpers/spaceHelpers.h>
#include <renderer/materials/MaterialXCpp/materials/adobeOpenPbr.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/shadingFrame.h>
#include <renderer/materials/MaterialXCpp/materials/disneyPrincipled.h>
#include <renderer/materials/MaterialXCpp/materials/gltfPbr.h>
#include <renderer/materials/MaterialXCpp/materials/openPbr.h>
#include <renderer/materials/MaterialXCpp/materials/standardSurface.h>
#include <renderer/materials/MaterialXCpp/materials/usdPreviewSurface.h>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace mxcpp {

Vec3f
ResolveGraphNormal(
    const SurfaceClosure& closure,
    const ShadingContext& ctx)
{
    // Resolve after the graph is fully assembled so surface-shader mixing has
    // selected the final authored normal. A graph without one inherits the
    // smooth exterior normal supplied by the shading context.
    Vec3f candidate;
    if (!closure.ResolveNormal(
            ctx.tangent, ctx.bitangent, ctx.normal, &candidate)) {
        return ctx.normal;
    }

    // Keep graph normals in the smooth normal's exterior hemisphere. This
    // gives every leaf one stable validation reference before incident-side
    // facing and geometric correction happen later.
    Vec3f normalShdWldExt;
    return Bsdf::detail::TryResolveShadingNormal(
        candidate, ctx.normal, &normalShdWldExt)
        ? normalShdWldExt
        : ctx.normal;
}

void
ValidateLeafNormals(
    Bsdf::ClosureTree* tree,
    const Vec3f& normalShdWldExt)
{
    // Legacy summary closures have no tree and therefore no leaf normals to
    // validate.
    if (!tree) {
        return;
    }

    for (Bsdf::Node& node : tree->nodes) {
        // Only surface leaves own shading normals. The generic parameter is
        // the C++17 std::visit exception; composite and volume nodes pass
        // through unchanged.
        std::visit([&](auto& data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (
                std::is_same_v<T, Bsdf::OrenNayarDiffuseData> ||
                std::is_same_v<T, Bsdf::BurleyDiffuseData> ||
                std::is_same_v<T, Bsdf::TranslucentData> ||
                std::is_same_v<T, Bsdf::SubsurfaceData> ||
                std::is_same_v<T, Bsdf::DielectricData> ||
                std::is_same_v<T, Bsdf::DielectricInterfaceData> ||
                std::is_same_v<T, Bsdf::ConductorData> ||
                std::is_same_v<T, Bsdf::GeneralizedSchlickData> ||
                std::is_same_v<T, Bsdf::SheenData> ||
                std::is_same_v<T, Bsdf::AdobeOpenPbrData>) {
                if (!data.hasShadingNormal) {
                    return;
                }

                // Store a finite unit exterior normal in every authored leaf
                // so preparation only needs to face and geometrically correct
                // it. Invalid values inherit the final graph normal rather
                // than being flipped into its hemisphere.
                Vec3f resolved;
                if (Bsdf::detail::TryResolveShadingNormal(
                        data, normalShdWldExt, &resolved)) {
                    data.normal = resolved;
                } else {
                    data.normal = normalShdWldExt;
                }
            }
        }, node.data);
    }
}

static const std::string _kSurface = "surface";
static const std::string _kVolume = "volume";
static const SlotName _kIn("in");
static const SlotName _kOut("out");
static const SlotName _kDisplacementInput("displacement");
static const SlotName _kScale("scale");
static const std::string _kSurfaceVolumeMaterial =
    "ND_hdembree_surface_volume_material";
static const std::string _kStandardSurface =
    "ND_standard_surface_surfaceshader";
static const std::string _kOpenPbr =
    "ND_open_pbr_surface_surfaceshader";
static const std::string _kDisneyPrincipled =
    "ND_disney_principled";
static const std::string _kGltfPbr =
    "ND_gltf_pbr_surfaceshader";
static const std::string _kUsdPreviewSurface = "UsdPreviewSurface";
static const std::string _kMaterialXUsdPreviewSurface =
    "ND_UsdPreviewSurface_surfaceshader";
static const std::string _kDisplacementFloat = "ND_displacement_float";
static const std::string _kSurfaceConstructor = "ND_surface";
static const std::string _kSurfaceUnlit = "ND_surface_unlit";
static const std::string _kVolumeConstructor = "ND_volume";
static const std::string _kMixSurfaceShader = "ND_mix_surfaceshader";
static const std::string _kMixVolumeShader = "ND_mix_volumeshader";
static const std::string _kDotSurfaceShader = "ND_dot_surfaceshader";
static const std::string _kConvertFloatSurfaceShader =
    "ND_convert_float_surfaceshader";
static const std::string _kConvertIntegerSurfaceShader =
    "ND_convert_integer_surfaceshader";
static const std::string _kConvertBooleanSurfaceShader =
    "ND_convert_boolean_surfaceshader";
static const std::string _kConvertColor3SurfaceShader =
    "ND_convert_color3_surfaceshader";
static const std::string _kConvertColor4SurfaceShader =
    "ND_convert_color4_surfaceshader";
static const std::string _kConvertVector2SurfaceShader =
    "ND_convert_vector2_surfaceshader";
static const std::string _kConvertVector3SurfaceShader =
    "ND_convert_vector3_surfaceshader";
static const std::string _kConvertVector4SurfaceShader =
    "ND_convert_vector4_surfaceshader";

enum class _ImplicitDefaultKind {
    Texcoord0,
    PositionObject,
    NormalObject,
    NormalWorld,
    TangentWorld,
    BitangentWorld,
    ViewDirectionWorld
};

struct _ImplicitInputDefault {
    const char* inputName;
    _ImplicitDefaultKind kind;
};

static bool
_StartsWith(const std::string& value, const char* prefix)
{
    return value.rfind(prefix, 0) == 0;
}

static bool
_MatchesAnyPrefix(
    const std::string& value,
    std::initializer_list<const char*> prefixes)
{
    for (const char* prefix : prefixes) {
        if (_StartsWith(value, prefix)) {
            return true;
        }
    }
    return false;
}

static bool
_MatchesAnyExact(
    const std::string& value,
    std::initializer_list<const char*> names)
{
    for (const char* name : names) {
        if (value == name) {
            return true;
        }
    }
    return false;
}

static GraphNode
_MakeImplicitDefaultNode(_ImplicitDefaultKind kind)
{
    GraphNode node;
    switch (kind) {
    case _ImplicitDefaultKind::Texcoord0:
        node.nodeTypeId = "ND_texcoord_vector2";
        node.parameters["index"] = Value(0);
        break;
    case _ImplicitDefaultKind::PositionObject:
        node.nodeTypeId = "ND_position_vector3";
        node.parameters["space"] = Value(std::string("object"));
        break;
    case _ImplicitDefaultKind::NormalObject:
        node.nodeTypeId = "ND_normal_vector3";
        node.parameters["space"] = Value(std::string("object"));
        break;
    case _ImplicitDefaultKind::NormalWorld:
        node.nodeTypeId = "ND_normal_vector3";
        node.parameters["space"] = Value(std::string("world"));
        break;
    case _ImplicitDefaultKind::TangentWorld:
        node.nodeTypeId = "ND_tangent_vector3";
        node.parameters["space"] = Value(std::string("world"));
        break;
    case _ImplicitDefaultKind::BitangentWorld:
        node.nodeTypeId = "ND_bitangent_vector3";
        node.parameters["space"] = Value(std::string("world"));
        break;
    case _ImplicitDefaultKind::ViewDirectionWorld:
        node.nodeTypeId = "ND_viewdirection_vector3";
        node.parameters["space"] = Value(std::string("world"));
        break;
    }
    return node;
}

static bool
_HasInputConnection(const GraphNode& node, const char* inputName)
{
    auto it = node.inputConnections.find(inputName);
    if (it == node.inputConnections.end()) {
        return false;
    }
    for (const GraphConnection& conn : it->second) {
        if (!conn.upstreamNode.empty()) {
            return true;
        }
    }
    return false;
}

static std::vector<_ImplicitInputDefault>
_GetImplicitInputDefaults(const std::string& nodeTypeId)
{
    std::vector<_ImplicitInputDefault> defaults;

    if (_MatchesAnyPrefix(nodeTypeId, {
            "ND_image_",
            "ND_tiledimage_",
            "ND_hextiledimage_",
            "ND_ramplr_",
            "ND_ramptb_",
            "ND_ramp4_",
            "ND_splitlr_",
            "ND_splittb_",
            "ND_noise2d_",
            "ND_fractal2d_",
            "ND_cellnoise2d_",
            "ND_worleynoise2d_",
            "ND_unifiednoise2d_",
            "ND_flake2d",
            "ND_checkerboard_",
            "ND_line_",
            "ND_circle_",
            "ND_cloverleaf_",
            "ND_hexagon_",
            "ND_grid_",
            "ND_crosshatch_",
            "ND_tiledcircles_",
            "ND_tiledcloverleafs_",
            "ND_tiledhexagons_",
            "ND_hextilednormalmap",
            "ND_heighttonormal_"})) {
        defaults.push_back({"texcoord", _ImplicitDefaultKind::Texcoord0});
    }

    if (_MatchesAnyPrefix(nodeTypeId, {
            "ND_triplanarprojection_",
            "ND_noise3d_",
            "ND_fractal3d_",
            "ND_cellnoise3d_",
            "ND_worleynoise3d_",
            "ND_unifiednoise3d_",
            "ND_flake3d"})) {
        defaults.push_back(
            {"position", _ImplicitDefaultKind::PositionObject});
    }

    if (_StartsWith(nodeTypeId, "ND_triplanarprojection_")) {
        defaults.push_back({"normal", _ImplicitDefaultKind::NormalObject});
    }

    if (_MatchesAnyPrefix(nodeTypeId, {
            "ND_bump_",
            "ND_normalmap",
            "ND_hextilednormalmap",
            "ND_reflect_",
            "ND_refract_"}) ||
        _MatchesAnyExact(nodeTypeId, {
            "ND_oren_nayar_diffuse_bsdf",
            "ND_burley_diffuse_bsdf",
            "ND_dielectric_bsdf",
            "ND_conductor_bsdf",
            "ND_generalized_schlick_bsdf",
            "ND_translucent_bsdf",
            "ND_subsurface_bsdf",
            "ND_sheen_bsdf",
            "ND_chiang_hair_bsdf",
            "ND_flake2d",
            "ND_flake3d",
            "ND_conical_edf",
            "ND_measured_edf",
            "ND_facingratio_float"})) {
        defaults.push_back({"normal", _ImplicitDefaultKind::NormalWorld});
    }

    if (_MatchesAnyPrefix(nodeTypeId, {
            "ND_bump_",
            "ND_normalmap",
            "ND_hextilednormalmap"}) ||
        _MatchesAnyExact(nodeTypeId, {
            "ND_dielectric_bsdf",
            "ND_conductor_bsdf",
            "ND_generalized_schlick_bsdf",
            "ND_flake2d",
            "ND_flake3d"})) {
        defaults.push_back({"tangent", _ImplicitDefaultKind::TangentWorld});
    }

    if (_MatchesAnyPrefix(nodeTypeId, {
            "ND_bump_",
            "ND_normalmap",
            "ND_hextilednormalmap"}) ||
        _MatchesAnyExact(nodeTypeId, {
            "ND_flake2d",
            "ND_flake3d"})) {
        defaults.push_back(
            {"bitangent", _ImplicitDefaultKind::BitangentWorld});
    }

    if (nodeTypeId == "ND_chiang_hair_bsdf") {
        defaults.push_back(
            {"curve_direction", _ImplicitDefaultKind::TangentWorld});
    }

    if (nodeTypeId == "ND_facingratio_float") {
        defaults.push_back(
            {"viewdirection", _ImplicitDefaultKind::ViewDirectionWorld});
    }

    return defaults;
}

static std::string
_MakeImplicitNodePath(
    const MaterialGraph& graph,
    const std::string& nodePath,
    const char* inputName)
{
    const std::string base = nodePath + "/__implicit_" + inputName;
    std::string path = base;
    int suffix = 1;
    while (graph.nodes.find(path) != graph.nodes.end()) {
        path = base + "_" + std::to_string(suffix++);
    }
    return path;
}

static void
_InjectImplicitDefaultConnections(MaterialGraph* graph)
{
    if (!graph) {
        return;
    }

    std::vector<std::pair<std::string, _ImplicitInputDefault>> injections;
    for (const auto& entry : graph->nodes) {
        for (const auto& defaultInput :
             _GetImplicitInputDefaults(entry.second.nodeTypeId)) {
            if (!_HasInputConnection(entry.second, defaultInput.inputName)) {
                injections.push_back({entry.first, defaultInput});
            }
        }
    }

    for (const auto& injection : injections) {
        const std::string path = _MakeImplicitNodePath(
            *graph, injection.first, injection.second.inputName);
        graph->nodes[path] = _MakeImplicitDefaultNode(injection.second.kind);

        GraphConnection conn;
        conn.upstreamNode = path;
        conn.upstreamOutputName = "out";
        graph->nodes[injection.first]
            .inputConnections[injection.second.inputName] = {std::move(conn)};
    }
}

static std::string
_MakeUniqueGraphNodePath(const MaterialGraph& graph, const char* baseName)
{
    std::string path = baseName;
    int suffix = 1;
    while (graph.nodes.find(path) != graph.nodes.end()) {
        path = std::string(baseName) + "_" + std::to_string(suffix++);
    }
    return path;
}

static void
_InjectSurfaceVolumeMaterialTerminal(MaterialGraph* graph)
{
    if (!graph) {
        return;
    }

    auto surfaceIt = graph->terminals.find(_kSurface);
    auto volumeIt = graph->terminals.find(_kVolume);
    if (volumeIt == graph->terminals.end()) {
        return;
    }

    // A volume terminal is a complete material even when no scattering
    // surface is authored. The empty surface input makes the resulting
    // closure a transparent medium boundary rather than an arbitrary
    // substitute terminal.
    GraphNode materialNode;
    materialNode.nodeTypeId = _kSurfaceVolumeMaterial;
    if (surfaceIt != graph->terminals.end()) {
        materialNode.inputConnections[_kSurface] = {surfaceIt->second};
    }
    materialNode.inputConnections[_kVolume] = {volumeIt->second};

    const std::string path = _MakeUniqueGraphNodePath(
        *graph, "/__hdembree_surface_volume_material");
    graph->nodes[path] = std::move(materialNode);

    GraphConnection conn;
    conn.upstreamNode = path;
    conn.upstreamOutputName = "out";
    graph->terminals[_kSurface] = std::move(conn);
}

// ---------------------------------------------------------------------------
// Compile
// ---------------------------------------------------------------------------

static bool
_IsGeomPropNode(const std::string& nodeTypeId)
{
    return _StartsWith(nodeTypeId, "ND_geompropvalue");
}

std::vector<std::string>
CollectGeomPropNames(const MaterialGraph& network)
{
    // Graph normalization does not synthesize geomprop nodes or rewrite their
    // geomprop parameters. If it starts doing either, collect from the same
    // normalized graph that Compile consumes so handle spaces cannot diverge.
    std::vector<std::string> names;
    for (const auto& [path, node] : network.nodes) {
        (void)path;
        if (!_IsGeomPropNode(node.nodeTypeId)) {
            continue;
        }
        const auto geomPropIt = node.parameters.find("geomprop");
        if (geomPropIt == node.parameters.end() ||
            !ValueHolds<std::string>(geomPropIt->second)) {
            continue;
        }
        const std::string& name =
            ValueGet<std::string>(geomPropIt->second);
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    return names;
}

CompileResult
EvalGraph::Compile(
    const MaterialGraph& network,
    const std::string& terminalName)
{
    const std::vector<std::string> geomPropNames =
        CollectGeomPropNames(network);
    return Compile(network, terminalName, geomPropNames);
}

CompileResult
EvalGraph::Compile(
    const MaterialGraph& network,
    const std::string& terminalName,
    const std::vector<std::string>& geomPropNames)
{
    enum class _VisitState {
        Unvisited,
        Visiting,
        Visited
    };

    NodeRegistry::RegisterBuiltinNodes();

    const std::string terminal = terminalName.empty()
        ? _kSurface : terminalName;
    MaterialGraph normalized = network;
    _InjectImplicitDefaultConnections(&normalized);
    if (terminal == _kSurface) {
        _InjectSurfaceVolumeMaterialTerminal(&normalized);
    }

    CompileResult result;

    // Locate the surface terminal.
    auto termIt = normalized.terminals.find(terminal);
    if (termIt == normalized.terminals.end()) {
        result.status = CompileStatus::AbsentTerminal;
        return result;
    }

    const std::string terminalNodePath = termIt->second.upstreamNode;
    auto termNodeIt = normalized.nodes.find(terminalNodePath);
    if (termNodeIt == normalized.nodes.end()) {
        result.diagnostic = "terminal \"" + terminal +
            "\" references missing node " + terminalNodePath;
        return result;
    }
    // Terminal models use dedicated dispatch rather than registry node
    // evaluators, so validate that dispatch surface explicitly.
    const std::string& terminalNodeType = termNodeIt->second.nodeTypeId;
    if (terminal == "displacement" &&
        terminalNodeType != _kDisplacementFloat) {
        result.diagnostic = "terminal \"displacement\" has unsupported node "
            "type " + terminalNodeType + " at " + terminalNodePath;
        return result;
    }
    const bool isDisplacementModel =
        terminalNodeType == _kDisplacementFloat;
    const bool isSurfaceModel = _EvalMaterialModel(
        terminalNodeType, ParamMap{}, EvalOptions{}, nullptr);
    if ((!isDisplacementModel && !isSurfaceModel) ||
        (terminal == _kSurface && !isSurfaceModel)) {
        result.diagnostic = "no evaluator registered for node type " +
            terminalNodeType + " at " + terminalNodePath;
        return result;
    }

    std::unique_ptr<EvalGraph> graph = std::make_unique<EvalGraph>();
    graph->_materialModelType = termNodeIt->second.nodeTypeId;

    // ---- Gather reachable nodes via DFS topological sort ----

    std::vector<std::string> sorted;
    std::map<std::string, _VisitState> visitStates;
    std::function<bool(
        const std::string&,
        const std::string&,
        const std::string&)> dfs =
        [&](const std::string& nodePath,
            const std::string& downstreamNodePath,
            const std::string& downstreamInputName) {
            const auto nodeIt = normalized.nodes.find(nodePath);
            if (nodeIt == normalized.nodes.end()) {
                const GraphNode& downstreamNode =
                    normalized.nodes.at(downstreamNodePath);
                result.diagnostic = downstreamNodePath + " (" +
                    downstreamNode.nodeTypeId + ") input " +
                    downstreamInputName + " references missing node " +
                    nodePath;
                return false;
            }

            _VisitState& state = visitStates[nodePath];
            if (state == _VisitState::Visited) {
                return true;
            }
            if (state == _VisitState::Visiting) {
                result.diagnostic =
                    "cycle detected through node " + nodePath;
                return false;
            }

            state = _VisitState::Visiting;

            for (const auto& entry : nodeIt->second.inputConnections) {
                // A dynamic geomprop name is malformed uniform authoring.
                // Ignore its upstream graph so this node can retain its
                // authored default even when that irrelevant graph is broken.
                if (_IsGeomPropNode(nodeIt->second.nodeTypeId) &&
                    entry.first == "geomprop") {
                    continue;
                }
                for (const auto& conn : entry.second) {
                    if (conn.upstreamNode == terminalNodePath) {
                        result.diagnostic =
                            "cycle detected through node " + terminalNodePath;
                        return false;
                    }
                    if (!dfs(conn.upstreamNode, nodePath, entry.first)) {
                        return false;
                    }
                }
            }

            state = _VisitState::Visited;
            sorted.push_back(nodePath);
            return true;
        };

    // Seed from the terminal node's upstream connections.
    for (const auto& entry : termNodeIt->second.inputConnections) {
        for (const auto& conn : entry.second) {
            if (conn.upstreamNode == terminalNodePath) {
                result.diagnostic =
                    "cycle detected through node " + terminalNodePath;
                return result;
            }
            if (!dfs(conn.upstreamNode, terminalNodePath, entry.first)) {
                return result;
            }
        }
    }

    // Build path → sorted-index map.
    std::map<std::string, int> nodeIndex;
    for (size_t i = 0; i < sorted.size(); ++i) {
        nodeIndex[sorted[i]] = static_cast<int>(i);
        const GraphNode& node = normalized.nodes.at(sorted[i]);
        // Only object-space position nodes require exact primitive
        // interpolation; world-space consumers use the transport hit.
        if (node.nodeTypeId == "ND_position_vector3") {
            const auto spaceIt = node.parameters.find("space");
            const bool worldSpace =
                spaceIt != node.parameters.end() &&
                ValueHolds<std::string>(spaceIt->second) &&
                NormalizeSpaceName(
                    ValueGet<std::string>(spaceIt->second),
                    std::string("object")) == "world";
            if (!worldSpace) {
                graph->_requiresObjectSpacePosition = true;
            }
        }
    }

    // ---- Build compiled nodes ----
    auto& registry = NodeRegistry::GetInstance();
    graph->_nodes.resize(sorted.size());

    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& path = sorted[i];
        const auto& node = normalized.nodes.at(path);
        auto& compiled = graph->_nodes[i];

        int geomPropHandle = -1;
        if (_IsGeomPropNode(node.nodeTypeId)) {
            const auto connectionIt =
                node.inputConnections.find("geomprop");
            const auto parameterIt = node.parameters.find("geomprop");
            const bool hasConnectedName =
                connectionIt != node.inputConnections.end() &&
                !connectionIt->second.empty();
            const bool hasConstantStringName =
                parameterIt != node.parameters.end() &&
                ValueHolds<std::string>(parameterIt->second);
            if (hasConnectedName || !hasConstantStringName) {
                // Keep one actionable warning while compiling a usable node.
                // Handle -1 makes the evaluator return its authored default.
                if (result.diagnostic.empty()) {
                    result.diagnostic = path + " (" + node.nodeTypeId +
                        ") input geomprop must be a constant string";
                }
            } else {
                const std::string& name =
                    ValueGet<std::string>(parameterIt->second);
                const auto nameIt = std::find(
                    geomPropNames.begin(), geomPropNames.end(), name);
                // CollectGeomPropNames and this rewrite operate on the same
                // authored geomprop parameters. Normalization must not create
                // a name that is absent from the material handle space.
                if (nameIt == geomPropNames.end()) {
                    result.diagnostic = path + " (" + node.nodeTypeId +
                        ") geomprop name is absent from the material handle "
                        "space";
                    return result;
                }
                geomPropHandle = static_cast<int>(
                    std::distance(geomPropNames.begin(), nameIt));
            }
        }

        compiled.evalFn = registry.Find(node.nodeTypeId);
        if (!compiled.evalFn) {
            result.diagnostic = "no evaluator registered for node type " +
                node.nodeTypeId + " at " + path;
            return result;
        }

        // Constant parameters.
        for (const auto& param : node.parameters) {
            InputBinding binding;
            binding.inputSlot = InternSlot(param.first);
            if (_IsGeomPropNode(node.nodeTypeId) &&
                param.first == "geomprop") {
                binding.defaultValue = Value(geomPropHandle);
            } else {
                binding.defaultValue = param.second;
            }
            compiled.inputs.push_back(std::move(binding));
        }
        if (_IsGeomPropNode(node.nodeTypeId) &&
            node.parameters.find("geomprop") == node.parameters.end()) {
            InputBinding binding;
            binding.inputSlot = InternSlot("geomprop");
            binding.defaultValue = Value(geomPropHandle);
            compiled.inputs.push_back(std::move(binding));
        }

        // Connections (may override a same-named constant).
        for (const auto& connEntry : node.inputConnections) {
            if (connEntry.second.empty()) continue;
            if (_IsGeomPropNode(node.nodeTypeId) &&
                connEntry.first == "geomprop") {
                continue;
            }
            const auto& conn = connEntry.second.front();

            auto idxIt = nodeIndex.find(conn.upstreamNode);
            if (idxIt == nodeIndex.end()) continue;

            bool replaced = false;
            for (auto& binding : compiled.inputs) {
                if (binding.inputSlot == InternSlot(connEntry.first)) {
                    binding.isConnected = true;
                    binding.sourceNodeIndex = idxIt->second;
                    binding.sourceOutputSlot = conn.upstreamOutputName.empty()
                        ? _kOut.Get()
                        : InternSlot(conn.upstreamOutputName);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                InputBinding binding;
                binding.inputSlot = InternSlot(connEntry.first);
                binding.isConnected = true;
                binding.sourceNodeIndex = idxIt->second;
                binding.sourceOutputSlot = conn.upstreamOutputName.empty()
                    ? _kOut.Get()
                    : InternSlot(conn.upstreamOutputName);
                compiled.inputs.push_back(std::move(binding));
            }
        }
    }

    // ---- Build terminal input bindings ----
    for (const auto& param : termNodeIt->second.parameters) {
        InputBinding binding;
        binding.inputSlot = InternSlot(param.first);
        binding.defaultValue = param.second;
        graph->_terminalInputs.push_back(std::move(binding));
    }

    for (const auto& connEntry : termNodeIt->second.inputConnections) {
        if (connEntry.second.empty()) continue;
        const auto& conn = connEntry.second.front();

        auto idxIt = nodeIndex.find(conn.upstreamNode);
        if (idxIt == nodeIndex.end()) continue;

        bool replaced = false;
        for (auto& binding : graph->_terminalInputs) {
            if (binding.inputSlot == InternSlot(connEntry.first)) {
                binding.isConnected = true;
                binding.sourceNodeIndex = idxIt->second;
                binding.sourceOutputSlot = conn.upstreamOutputName.empty()
                    ? _kOut.Get()
                    : InternSlot(conn.upstreamOutputName);
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            InputBinding binding;
            binding.inputSlot = InternSlot(connEntry.first);
            binding.isConnected = true;
            binding.sourceNodeIndex = idxIt->second;
            binding.sourceOutputSlot = conn.upstreamOutputName.empty()
                ? _kOut.Get()
                : InternSlot(conn.upstreamOutputName);
            graph->_terminalInputs.push_back(std::move(binding));
        }
    }

    graph->_isValid = true;
    result.status = CompileStatus::Valid;
    result.graph = std::move(graph);
    return result;
}

// ---------------------------------------------------------------------------
// Evaluate
// ---------------------------------------------------------------------------

void
EvalGraph::_BuildParamMap(
    const std::vector<InputBinding>& bindings,
    const std::vector<NodeOutputMap>& nodeOutputs,
    ParamMap* params) const
{
    params->Clear();
    params->Reserve(bindings.size());

    for (const auto& binding : bindings) {
        if (binding.isConnected && binding.sourceNodeIndex >= 0) {
            const Value* value = nullptr;
            if (static_cast<size_t>(binding.sourceNodeIndex) < nodeOutputs.size()) {
                value = nodeOutputs[binding.sourceNodeIndex].Find(
                    binding.sourceOutputSlot);
            }

            params->Add(
                binding.inputSlot,
                value,
                &_ReevaluateInput,
                this,
                binding.sourceNodeIndex,
                binding.sourceOutputSlot);
            continue;
        }

        if (!ValueIsEmpty(binding.defaultValue)) {
            params->Add(binding.inputSlot, &binding.defaultValue);
        }
    }
}

void
EvalGraph::_EvaluateNodes(
    const ShadingContext& ctx,
    size_t nodeCount,
    EvalScratch* scratch) const
{
    if (scratch->nodeOutputs.size() < nodeCount) {
        scratch->nodeOutputs.resize(nodeCount);
    }
    if (scratch->nodeInputs.size() < nodeCount) {
        scratch->nodeInputs.resize(nodeCount);
    }

    for (size_t i = 0; i < nodeCount; ++i) {
        const auto& node = _nodes[i];
        if (!node.evalFn) {
            continue;
        }

        auto& inputs = scratch->nodeInputs[i];
        _BuildParamMap(node.inputs, scratch->nodeOutputs, &inputs);

        auto& outputs = scratch->nodeOutputs[i];
        outputs.Clear();
        node.evalFn(inputs, ctx, &outputs);
    }
}

bool
EvalGraph::_EvaluateNodeOutput(
    int nodeIndex,
    SlotId outputSlot,
    const ShadingContext& ctx,
    Value* out) const
{
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= _nodes.size()) {
        return false;
    }

    // This runs inside an outer Evaluate() whose thread_local scratch holds
    // the values that downstream nodes are still reading, so a nested
    // re-evaluation needs its own scratch.  It is also extremely hot: texture
    // nodes re-evaluate their texcoord input three times per tap to build a
    // filter footprint, so constructing a fresh EvalScratch here made every
    // connected-texture material eval allocate and free dozens of containers,
    // and the resulting allocator contention dominated textured-material
    // shading cost on many-core renders.  Keep a per-thread, depth-indexed
    // pool of scratches instead; entries grow once and are reused for the
    // lifetime of the thread.
    thread_local std::vector<std::unique_ptr<EvalScratch>> nestedScratchPool;
    thread_local size_t nestedScratchDepth = 0;
    if (nestedScratchDepth >= nestedScratchPool.size()) {
        nestedScratchPool.emplace_back(std::make_unique<EvalScratch>());
    }
    EvalScratch* const scratch = nestedScratchPool[nestedScratchDepth].get();

    struct _DepthGuard {
        size_t& depth;
        explicit _DepthGuard(size_t& d) : depth(d) { ++depth; }
        ~_DepthGuard() { --depth; }
    } depthGuard(nestedScratchDepth);

    const size_t nodeCount = static_cast<size_t>(nodeIndex) + 1;
    _EvaluateNodes(ctx, nodeCount, scratch);

    const Value* value = scratch->nodeOutputs[nodeIndex].Find(outputSlot);
    if (!value) {
        return false;
    }

    if (out) {
        *out = *value;
    }
    return true;
}

/* static */
bool
EvalGraph::_ReevaluateInput(
    const void* userData,
    int sourceNodeIndex,
    SlotId sourceOutputSlot,
    const ShadingContext& ctx,
    Value* out)
{
    if (!userData) {
        return false;
    }

    const auto* graph = static_cast<const EvalGraph*>(userData);
    return graph->_EvaluateNodeOutput(
        sourceNodeIndex, sourceOutputSlot, ctx, out);
}

SurfaceClosure
EvalGraph::Evaluate(const ShadingContext& ctx, const EvalOptions& options) const
{
    if (!_isValid) {
        return SurfaceClosure();
    }

    thread_local EvalScratch scratch;
    // Grow-only: avoid shrink/regrow thrashing when graphs of different
    // sizes share the same thread_local scratch.
    if (scratch.nodeOutputs.size() < _nodes.size()) {
        scratch.nodeOutputs.resize(_nodes.size());
    }
    if (scratch.nodeInputs.size() < _nodes.size()) {
        scratch.nodeInputs.resize(_nodes.size());
    }
    _EvaluateNodes(ctx, _nodes.size(), &scratch);

    // Gather terminal parameters.
    auto& terminalParams = scratch.terminalParams;
    _BuildParamMap(_terminalInputs, scratch.nodeOutputs, &terminalParams);

    SurfaceClosure closure;
    if (!_EvalMaterialModel(
            _materialModelType, terminalParams, options, &closure)) {
        return SurfaceClosure();
    }
    closure.luminanceCoefficients = ctx.luminanceCoefficients;
    closure.bsdfTree.luminanceCoefficients = ctx.luminanceCoefficients;
    return closure;
}

bool
EvalGraph::EvaluateDisplacement(
    const ShadingContext& ctx,
    float* displacement) const
{
    if (!_isValid || _materialModelType != _kDisplacementFloat ||
        !displacement) {
        return false;
    }

    thread_local EvalScratch scratch;
    if (scratch.nodeOutputs.size() < _nodes.size()) {
        scratch.nodeOutputs.resize(_nodes.size());
    }
    if (scratch.nodeInputs.size() < _nodes.size()) {
        scratch.nodeInputs.resize(_nodes.size());
    }
    _EvaluateNodes(ctx, _nodes.size(), &scratch);

    auto& terminalParams = scratch.terminalParams;
    _BuildParamMap(_terminalInputs, scratch.nodeOutputs, &terminalParams);
    // Keep ND_displacement_float semantics here rather than in Embree: scale
    // is part of the MaterialX terminal contract, while Embree only consumes
    // the resulting signed scalar.
    *displacement =
        ValueGetter<float>::Get(
            terminalParams, _kDisplacementInput, 0.0f) *
        ValueGetter<float>::Get(terminalParams, _kScale, 1.0f);
    return true;
}

// ---------------------------------------------------------------------------
// Material model dispatch
// ---------------------------------------------------------------------------

/* static */
bool
EvalGraph::_EvalMaterialModel(
    const std::string& modelType,
    const ParamMap& params,
    const EvalOptions& options,
    SurfaceClosure* closure)
{
    if (modelType == _kSurfaceVolumeMaterial) {
        if (closure) {
            *closure = EvalSurfaceVolumeMaterial(params);
        }
        return true;
    }
    if (modelType == _kStandardSurface) {
        if (closure) {
            *closure = EvalStandardSurface(params);
        }
        return true;
    }
    if (modelType == _kOpenPbr) {
        if (closure) {
            if (options.useAdobeOpenPBR) {
                *closure = options.visibilityOnly
                    ? EvalAdobeOpenPbrVisibility(params)
                    : EvalAdobeOpenPbr(params);
            } else {
                *closure = EvalOpenPbr(params);
            }
        }
        return true;
    }
    if (modelType == _kDisneyPrincipled) {
        if (closure) {
            *closure = EvalDisneyPrincipled(params);
        }
        return true;
    }
    if (modelType == _kGltfPbr) {
        if (closure) {
            *closure = EvalGltfPbr(params);
        }
        return true;
    }
    if (modelType == _kUsdPreviewSurface) {
        if (closure) {
            *closure = EvalUsdPreviewSurface(params);
        }
        return true;
    }
    if (modelType == _kMaterialXUsdPreviewSurface) {
        if (closure) {
            *closure = EvalUsdPreviewSurface(params);
        }
        return true;
    }
    if (modelType == _kSurfaceConstructor) {
        if (closure) {
            *closure = EvalSurfaceConstructor(params);
        }
        return true;
    }
    if (modelType == _kSurfaceUnlit) {
        if (closure) {
            *closure = EvalSurfaceUnlit(params);
        }
        return true;
    }
    if (modelType == _kVolumeConstructor) {
        if (closure) {
            *closure = EvalVolumeConstructor(params);
        }
        return true;
    }
    if (modelType == _kMixVolumeShader) {
        if (closure) {
            *closure = EvalMixVolumeShader(params);
        }
        return true;
    }
    if (modelType == _kMixSurfaceShader) {
        if (closure) {
            static const SlotName bg("bg");
            static const SlotName fg("fg");
            static const SlotName mix("mix");

            const SurfaceClosure empty = MakeEmptySurfaceClosure();
            *closure = MixSurfaceClosures(
                Get<SurfaceClosure>(params, bg, empty),
                Get<SurfaceClosure>(params, fg, empty),
                Get<float>(params, mix, 0.0f));
        }
        return true;
    }
    if (modelType == _kDotSurfaceShader) {
        if (closure) {
            *closure = Get<SurfaceClosure>(
                params, _kIn, MakeEmptySurfaceClosure());
        }
        return true;
    }
    if (modelType == _kConvertFloatSurfaceShader) {
        if (closure) {
            const float v = Get<float>(params, _kIn, 0.0f);
            *closure = MakeUnlitSurfaceClosure(Vec3f(v));
        }
        return true;
    }
    if (modelType == _kConvertIntegerSurfaceShader) {
        if (closure) {
            const float v = static_cast<float>(Get<int>(params, _kIn, 0));
            *closure = MakeUnlitSurfaceClosure(Vec3f(v));
        }
        return true;
    }
    if (modelType == _kConvertBooleanSurfaceShader) {
        if (closure) {
            const float v = Get<bool>(params, _kIn, false) ? 1.0f : 0.0f;
            *closure = MakeUnlitSurfaceClosure(Vec3f(v));
        }
        return true;
    }
    if (modelType == _kConvertColor3SurfaceShader ||
        modelType == _kConvertVector3SurfaceShader) {
        if (closure) {
            *closure = MakeUnlitSurfaceClosure(
                Get<Vec3f>(params, _kIn, Vec3f(0.0f)));
        }
        return true;
    }
    if (modelType == _kConvertColor4SurfaceShader ||
        modelType == _kConvertVector4SurfaceShader) {
        if (closure) {
            const Vec4f v = Get<Vec4f>(
                params, _kIn, Vec4f(0.0f, 0.0f, 0.0f, 1.0f));
            *closure =
                MakeUnlitSurfaceClosure(Vec3f(v[0], v[1], v[2]), v[3]);
        }
        return true;
    }
    if (modelType == _kConvertVector2SurfaceShader) {
        if (closure) {
            const Vec2f v = Get<Vec2f>(params, _kIn, Vec2f(0.0f));
            *closure =
                MakeUnlitSurfaceClosure(Vec3f(v[0], v[1], 0.0f));
        }
        return true;
    }

    return false;
}

}  // namespace mxcpp
