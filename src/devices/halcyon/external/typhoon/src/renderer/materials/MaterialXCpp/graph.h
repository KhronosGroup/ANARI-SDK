//
// MaterialXCpp evaluation graph — pxr-independent.
//
#ifndef MXCPP_GRAPH_H
#define MXCPP_GRAPH_H

#include "graphTypes.h"
#include "nodeRegistry.h"
#include "paramMap.h"
#include "surfaceClosure.h"

#include <memory>
#include <string>
#include <vector>

namespace mxcpp {

enum class CompileStatus {
    Valid,
    Invalid,
    AbsentTerminal
};

struct CompileResult;

struct EvalOptions
{
    bool useAdobeOpenPBR = false;
    bool visibilityOnly = false;
};

/// Resolves the final closure's graph normal through the exterior tangent
/// frame in `ctx`.
///
/// `closure` must be fully assembled. `ctx.normal` must be a finite unit
/// exterior normal. Invalid, degenerate, or opposite-hemisphere authored
/// normals fall back to `ctx.normal` without negation. Does not modify
/// `closure` and does not throw.
Vec3f ResolveGraphNormal(
    const SurfaceClosure& closure,
    const ShadingContext& ctx);

/// Validates authored leaf normals against `normalShdWldExt`.
///
/// `tree` must be non-null and unprepared. `normalShdWldExt` must be the
/// finite unit exterior normal returned by `ResolveGraphNormal()` for the
/// same closure. Accepted normals remain in the exterior frame; invalid
/// values fall back without negation. Does not face or geometrically correct
/// normals. An empty tree is a no-op. Does not throw.
void ValidateLeafNormals(
    Bsdf::ClosureTree* tree,
    const Vec3f& normalShdWldExt);

/// Collect the ordered, de-duplicated geomprop handle space for a material.
///
/// Only constant string geomprop parameters receive handles. Graph
/// normalization must not synthesize geomprop nodes or parameters after this
/// table is collected.
std::vector<std::string> CollectGeomPropNames(
    const MaterialGraph& network);

/// \class EvalGraph
///
/// A compiled, topologically-sorted shader graph ready for per-pixel
/// sequential evaluation.
class EvalGraph
{
public:
    /// Compile one terminal of a MaterialGraph without using authored graph
    /// errors as exceptions.
    ///
    /// \p terminalName selects the terminal. An empty name selects "surface".
    /// A valid result owns the only non-null graph. Invalid results carry one
    /// actionable diagnostic. An absent requested terminal carries neither.
    static CompileResult Compile(
        const MaterialGraph& network,
        const std::string& terminalName = std::string());

    /// Compile against the material-owned geomprop handle space.
    ///
    /// The resulting graph is valid only while evaluated against resolved
    /// bindings indexed by \p geomPropNames.
    static CompileResult Compile(
        const MaterialGraph& network,
        const std::string& terminalName,
        const std::vector<std::string>& geomPropNames);

    /// Evaluate this valid graph for one shading point.
    ///
    /// Malformed authored values, missing inputs, and type mismatches produce
    /// node or material fallbacks rather than exceptions. Renderer callbacks
    /// in ShadingContext must likewise translate recoverable backend failures
    /// into their documented fallback results.
    SurfaceClosure Evaluate(
        const ShadingContext& ctx,
        const EvalOptions& options = EvalOptions()) const;

    /// Evaluate an ND_displacement_float terminal without throwing for
    /// malformed authored values, missing inputs, or type mismatches.
    ///
    /// Returns false when this is not a displacement graph, \p displacement
    /// is null, or evaluation cannot produce a float.
    bool EvaluateDisplacement(
        const ShadingContext& ctx,
        float* displacement) const;

    bool IsValid() const { return _isValid; }
    /// Whether a reachable node requires exact primitive-interpolated
    /// object-space position rather than a world-position round trip.
    bool RequiresObjectSpacePosition() const
    {
        return _requiresObjectSpacePosition;
    }

private:
    struct InputBinding {
        SlotId inputSlot = InvalidSlotId;
        bool isConnected = false;
        int sourceNodeIndex = -1;
        SlotId sourceOutputSlot = InvalidSlotId;
        Value defaultValue;
    };

    struct CompiledNode {
        NodeEvalFn evalFn = nullptr;
        std::vector<InputBinding> inputs;
    };

    struct EvalScratch {
        std::vector<NodeOutputMap> nodeOutputs;
        std::vector<ParamMap> nodeInputs;
        ParamMap terminalParams;
    };

    std::vector<CompiledNode> _nodes;
    std::vector<InputBinding> _terminalInputs;
    std::string _materialModelType;
    bool _isValid = false;
    bool _requiresObjectSpacePosition = false;

    /// Evaluate one supported terminal model into \p closure. A null closure
    /// performs the same dispatch lookup without evaluating the model.
    ///
    /// Returns false when \p modelType has no terminal-model evaluator.
    static bool _EvalMaterialModel(
        const std::string& modelType,
        const ParamMap& params,
        const EvalOptions& options,
        SurfaceClosure* closure);

    void _BuildParamMap(
        const std::vector<InputBinding>& bindings,
        const std::vector<NodeOutputMap>& nodeOutputs,
        ParamMap* params) const;

    void _EvaluateNodes(
        const ShadingContext& ctx,
        size_t nodeCount,
        EvalScratch* scratch) const;

    bool _EvaluateNodeOutput(
        int nodeIndex,
        SlotId outputSlot,
        const ShadingContext& ctx,
        Value* out) const;

    static bool _ReevaluateInput(
        const void* userData,
        int sourceNodeIndex,
        SlotId sourceOutputSlot,
        const ShadingContext& ctx,
        Value* out);
};

/// The explicit outcome of compiling one requested material terminal.
///
/// Valid owns a usable graph and may carry one recoverable authoring
/// diagnostic. Invalid owns no graph and carries one fatal diagnostic.
/// AbsentTerminal owns no graph and no diagnostic.
struct CompileResult
{
    CompileStatus status = CompileStatus::Invalid;
    std::unique_ptr<EvalGraph> graph;
    std::string diagnostic;
};

}  // namespace mxcpp

#endif  // MXCPP_GRAPH_H
