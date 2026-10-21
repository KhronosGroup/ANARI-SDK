//
// MaterialXCpp node registry — pxr-independent.
//
#ifndef MXCPP_NODE_REGISTRY_H
#define MXCPP_NODE_REGISTRY_H

#include "paramMap.h"

#include <renderer/materials/MaterialXCpp/nodes/helpers/valueHelper.h>

#include <algorithm>
#include <string>
#include <unordered_map>

namespace mxcpp {

/// Evaluation function signature for a single node.
using NodeEvalFn = void (*)(
    const ParamMap& inputs,
    const ShadingContext& ctx,
    NodeOutputMap* outputs);

/// \class NodeRegistry
///
/// Singleton registry mapping MaterialX node type identifiers to
/// C++ evaluation functions.
class NodeRegistry
{
public:
    static NodeRegistry& GetInstance();

    void Register(const std::string& nodeTypeId, NodeEvalFn fn);
    NodeEvalFn Find(const std::string& nodeTypeId) const;

    /// Ensures all built-in node categories are registered. Thread-safe,
    /// idempotent. Called automatically by EvalGraph::Compile().
    static void RegisterBuiltinNodes();

private:
    NodeRegistry() = default;

    std::unordered_map<std::string, NodeEvalFn> _nodes;
};

}  // namespace mxcpp

#endif  // MXCPP_NODE_REGISTRY_H
